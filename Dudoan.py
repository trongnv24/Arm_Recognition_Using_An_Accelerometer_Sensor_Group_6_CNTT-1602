import numpy as np
import pandas as pd
from tensorflow.keras.models import load_model
import joblib
import os
from flask import Flask, request, jsonify, render_template, send_from_directory
from collections import deque
import time
import threading
import json

# ===== CẤU HÌNH ĐƯỜNG DẪN (BẠN CÓ THỂ CHỈNH SỬA Ở ĐÂY) =====
# Đường dẫn đến các file mô hình, scaler, encoder
MODEL_PATH = "LSTM4/lstm_gesture_model3.h5"
SCALER_PATH = "LSTM4/scaler3.pkl"
LABEL_ENCODER_PATH = "LSTM4/label_encoder3.pkl"

# Đường dẫn đến thư mục chứa các file tĩnh (HTML, JS, CSS)
STATIC_FOLDER = "static"
TEMPLATES_FOLDER = "templates"

# Cấu hình Flask server
HOST = '0.0.0.0'  # Lắng nghe tất cả các kết nối
PORT = 5000       # Port của server

# Số bước thời gian cho mỗi chuỗi đầu vào (phải giống với giá trị khi huấn luyện)
TIME_STEPS = 50

# Tạo thư mục static và templates nếu chưa tồn tại
os.makedirs(STATIC_FOLDER, exist_ok=True)
os.makedirs(TEMPLATES_FOLDER, exist_ok=True)

# Khởi tạo Flask app
app = Flask(__name__, 
            static_folder=STATIC_FOLDER, 
            template_folder=TEMPLATES_FOLDER)

# Lớp JSONEncoder tùy chỉnh để xử lý numpy types
class NumpyEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, (np.integer, np.int64, np.int32)):
            return int(obj)
        elif isinstance(obj, (np.floating, np.float64, np.float32)):
            return float(obj)
        elif isinstance(obj, np.ndarray):
            return obj.tolist()
        return super(NumpyEncoder, self).default(obj)

# Cấu hình Flask sử dụng JSONEncoder tùy chỉnh
app.json.encoder = NumpyEncoder

# Khởi tạo biến toàn cục
model = None
scaler = None
label_encoder = None
sensor_buffer = deque(maxlen=500)  # Tăng kích thước buffer để lưu nhiều mẫu hơn
debug_buffer = deque(maxlen=100)  # Lưu dữ liệu để debug
current_gesture = "Chưa có dự đoán"
current_confidence = 0.0
prediction_lock = threading.Lock()  # Lock để tránh xung đột khi cập nhật dự đoán
all_predictions = {}  # Dict lưu tất cả các nhãn đã dự đoán và số lần xuất hiện
is_collecting = False  # Trạng thái thu thập dữ liệu
collection_start_time = None  # Thời điểm bắt đầu thu thập
samples_received = 0  # Số lượng mẫu đã nhận

def load_resources():
    """Tải mô hình, scaler và label encoder"""
    global model, scaler, label_encoder
    
    print(f"Đang tải mô hình từ {MODEL_PATH}...")
    model = load_model(MODEL_PATH)
    
    print(f"Đang tải scaler từ {SCALER_PATH}...")
    scaler = joblib.load(SCALER_PATH)
    
    print(f"Đang tải label encoder từ {LABEL_ENCODER_PATH}...")
    label_encoder = joblib.load(LABEL_ENCODER_PATH)
    
    # In thông tin về mô hình
    print("\nThông tin mô hình:")
    model.summary()
    
    # In thông tin về label encoder
    print("\nDanh sách các nhãn:")
    print(label_encoder.classes_)
    
    print("Đã tải xong tất cả tài nguyên!")

def predict_gesture(debug=False):
    """Thực hiện dự đoán dựa trên dữ liệu trong buffer - sử dụng cùng quy trình như CSV"""
    global current_gesture, current_confidence, all_predictions
    
    if len(sensor_buffer) < TIME_STEPS:
        return "Chưa đủ dữ liệu", 0.0, None
    
    # Chuyển buffer thành mảng numpy
    features = np.array(list(sensor_buffer))
    
    # Lưu dữ liệu gốc trước khi chuẩn hóa (để debug)
    original_features = features.copy()
    
    # Chuẩn hóa dữ liệu
    features_scaled = scaler.transform(features)
    
    # Lưu dữ liệu đã chuẩn hóa (để debug)
    scaled_features = features_scaled.copy()
    
    # === QUAN TRỌNG: Tạo sequences giống như trong phương thức CSV ===
    sequences = []
    for i in range(len(features_scaled) - TIME_STEPS + 1):
        sequences.append(features_scaled[i:i + TIME_STEPS])
    
    sequences = np.array(sequences)
    
    # Dự đoán - sử dụng batch thay vì một mẫu
    predictions = model.predict(sequences, verbose=0)
    
    # Tính toán dự đoán tổng hợp từ tất cả chuỗi
    # Cách 1: Lấy cử chỉ phổ biến nhất
    predicted_indices = np.argmax(predictions, axis=1)
    predicted_labels = label_encoder.inverse_transform(predicted_indices)
    confidences = np.max(predictions, axis=1) * 100  # Chuyển về phần trăm
    
    # Đếm số lượng mỗi nhãn
    unique_labels, counts = np.unique(predicted_labels, return_counts=True)
    label_counts = dict(zip(unique_labels, counts))
    
    # Lấy nhãn phổ biến nhất
    if len(label_counts) > 0:
        predicted_label = max(label_counts, key=label_counts.get)
        count = label_counts[predicted_label]
        confidence = (count / len(predicted_labels)) * 100
    else:
        predicted_label = "Không xác định"
        confidence = 0.0
    
    # Đảm bảo predicted_label là kiểu Python gốc (str hoặc int)
    if isinstance(predicted_label, (np.integer, np.int64, np.int32)):
        predicted_label = int(predicted_label)
    elif isinstance(predicted_label, (np.floating, np.float64, np.float32)):
        predicted_label = float(predicted_label)
    elif isinstance(predicted_label, np.ndarray):
        predicted_label = predicted_label.tolist()
    
    # Cập nhật số lần xuất hiện của nhãn này
    predicted_label_str = str(predicted_label)  # Chuyển đổi thành string để làm key
    if predicted_label_str not in all_predictions:
        all_predictions[predicted_label_str] = 0
    all_predictions[predicted_label_str] += 1
    
    # Thông tin debug
    debug_info = None
    if debug:
        # Tính toán độ tin cậy cho từng nhãn
        all_class_probabilities = {}
        for i, unique_label in enumerate(unique_labels):
            label_mask = predicted_labels == unique_label
            avg_confidence = np.mean(confidences[label_mask])
            all_class_probabilities[str(unique_label)] = float(avg_confidence)
        
        # Tạo thông tin debug
        debug_info = {
            "original_data": original_features[-3:].tolist(),  # Chỉ lấy 3 mẫu cuối
            "scaled_data": scaled_features[-3:].tolist(),     # Chỉ lấy 3 mẫu cuối
            "num_sequences": len(sequences),
            "predictions_breakdown": {
                "total_predictions": len(predicted_labels),
                "label_counts": {str(k): int(v) for k, v in label_counts.items()},
                "average_confidences": all_class_probabilities
            }
        }
    
    return predicted_label, confidence, debug_info

@app.route('/')
def index():
    """Trang chủ với dashboard hiển thị kết quả dự đoán và debug"""
    return render_template('index.html')

@app.route('/sensor-data', methods=['POST'])
def receive_sensor_data():
    """
    Endpoint để nhận dữ liệu cảm biến từ client
    Hỗ trợ 2 định dạng:
    1. Dữ liệu đơn: {"AccX": float, "AccY": float, "AccZ": float, "GyroX": float, "GyroY": float, "GyroZ": float}
    2. Dữ liệu batch: {"samples": [{"AccX": float, "AccY": float, ...}, ...]}
    """
    global is_collecting, samples_received
    
    try:
        data = request.json
        
        # Nếu đang không thu thập dữ liệu, trả về message thông báo
        if not is_collecting:
            return jsonify({
                "status": "ignored", 
                "message": "Không trong trạng thái thu thập dữ liệu",
                "collecting": False
            }), 200
        
        # Kiểm tra xem có phải là dữ liệu batch hay không
        if "samples" in data:
            # Xử lý dữ liệu batch
            samples = data["samples"]
            with prediction_lock:
                for sample in samples:
                    # Kiểm tra dữ liệu đầu vào
                    required_keys = ['AccX', 'AccY', 'AccZ', 'GyroX', 'GyroY', 'GyroZ']
                    if not all(key in sample for key in required_keys):
                        continue  # Bỏ qua mẫu không hợp lệ
                    
                    # Trích xuất giá trị cảm biến
                    sensor_values = [
                        float(sample['AccX']), 
                        float(sample['AccY']), 
                        float(sample['AccZ']),
                        float(sample['GyroX']), 
                        float(sample['GyroY']), 
                        float(sample['GyroZ'])
                    ]
                    
                    # Thêm vào buffer
                    sensor_buffer.append(sensor_values)
                    samples_received += 1
            
            # In thông tin về số lượng mẫu
            print(f"Nhận {len(samples)} mẫu, tổng số mẫu đã nhận: {samples_received}")
            
            return jsonify({
                "status": "success", 
                "buffer_size": len(sensor_buffer),
                "samples_received": samples_received,
                "samples_in_batch": len(samples)
            }), 200
        else:
            # Kiểm tra dữ liệu đầu vào cho định dạng đơn
            required_keys = ['AccX', 'AccY', 'AccZ', 'GyroX', 'GyroY', 'GyroZ']
            if not all(key in data for key in required_keys):
                return jsonify({"error": "Thiếu trường dữ liệu, cần có: AccX, AccY, AccZ, GyroX, GyroY, GyroZ"}), 400
            
            # Trích xuất giá trị cảm biến
            sensor_values = [
                float(data['AccX']), 
                float(data['AccY']), 
                float(data['AccZ']),
                float(data['GyroX']), 
                float(data['GyroY']), 
                float(data['GyroZ'])
            ]
            
            # Thêm vào buffer
            with prediction_lock:
                sensor_buffer.append(sensor_values)
                samples_received += 1
            
            return jsonify({
                "status": "success", 
                "buffer_size": len(sensor_buffer),
                "samples_received": samples_received
            }), 200
    
    except Exception as e:
        return jsonify({"error": str(e)}), 400

@app.route('/start-collection', methods=['POST'])
def start_collection():
    """Endpoint để bắt đầu thu thập dữ liệu"""
    global is_collecting, collection_start_time, sensor_buffer, samples_received
    
    with prediction_lock:
        # Xóa buffer cũ
        sensor_buffer.clear()
        is_collecting = True
        collection_start_time = time.time()
        samples_received = 0
        
    return jsonify({
        "status": "success", 
        "message": "Bắt đầu thu thập dữ liệu",
        "collection_start_time": collection_start_time
    })

@app.route('/stop-collection-and-predict', methods=['POST'])
def stop_collection_and_predict():
    """Endpoint để dừng thu thập và thực hiện dự đoán"""
    global is_collecting, current_gesture, current_confidence, debug_buffer
    
    with prediction_lock:
        is_collecting = False
        
        # Thực hiện dự đoán
        if len(sensor_buffer) >= TIME_STEPS:
            gesture, confidence, debug_info = predict_gesture(debug=True)
            
            # Cập nhật kết quả dự đoán
            current_gesture = gesture
            current_confidence = confidence
            
            # Lưu thông tin debug
            if debug_info:
                debug_entry = {
                    "time": time.strftime("%H:%M:%S"),
                    "gesture": gesture,
                    "confidence": confidence,
                    "debug_info": debug_info
                }
                debug_buffer.append(debug_entry)
                
            message = f"Dự đoán thành công: {gesture} ({confidence:.2f}%)"
        else:
            message = f"Không đủ dữ liệu để dự đoán (cần ít nhất {TIME_STEPS} mẫu)"
    
    return jsonify({
        "status": "success", 
        "message": message,
        "gesture": current_gesture,
        "confidence": current_confidence,
        "buffer_size": len(sensor_buffer),
        "samples_received": samples_received
    })

@app.route('/prediction', methods=['GET'])
def get_prediction():
    """Endpoint để lấy kết quả dự đoán hiện tại"""
    with prediction_lock:
        result = {
            "gesture": current_gesture,
            "confidence": current_confidence,
            "buffer_size": len(sensor_buffer),
            "time": time.strftime("%H:%M:%S"),
            "is_collecting": is_collecting,
            "collection_start_time": collection_start_time,
            "samples_received": samples_received
        }
    return jsonify(result)

@app.route('/current-data', methods=['GET'])
def get_current_data():
    """Endpoint để lấy dữ liệu cảm biến hiện tại"""
    with prediction_lock:
        # Chuyển deque thành list Python thuần
        latest_data = list(sensor_buffer)
    return jsonify({"latest_data": latest_data})

@app.route('/debug-data', methods=['GET'])
def get_debug_data():
    """Endpoint để lấy dữ liệu debug"""
    with prediction_lock:
        debug_data = list(debug_buffer)
    return jsonify(debug_data)

@app.route('/stats', methods=['GET'])
def get_stats():
    """Endpoint để lấy thống kê dự đoán"""
    with prediction_lock:
        stats = {
            "predictions": all_predictions,
            "total_predictions": sum(all_predictions.values()),
            "samples_received": samples_received
        }
    return jsonify(stats)

@app.route('/reset', methods=['POST'])
def reset_stats():
    """Endpoint để reset thống kê dự đoán"""
    global all_predictions, debug_buffer, samples_received
    with prediction_lock:
        all_predictions = {}
        debug_buffer.clear()
        samples_received = 0
    return jsonify({"status": "success", "message": "Đã reset thống kê dự đoán"})

@app.route('/clear-buffer', methods=['POST'])
def clear_buffer():
    """Endpoint để xóa buffer dữ liệu cảm biến"""
    global sensor_buffer
    with prediction_lock:
        sensor_buffer.clear()
    return jsonify({"status": "success", "message": "Đã xóa buffer dữ liệu"})

@app.route('/status', methods=['GET'])
def get_status():
    """Endpoint để kiểm tra trạng thái server"""
    return jsonify({
        "status": "running",
        "buffer_size": len(sensor_buffer),
        "model_loaded": model is not None,
        "time": time.strftime("%H:%M:%S"),
        "is_collecting": is_collecting,
        "samples_received": samples_received
    })

def start_server():
    """Khởi động server Flask"""
    # Tải mô hình và các tài nguyên cần thiết
    load_resources()
    
    # Khởi động server Flask
    print(f"Khởi động server tại http://{HOST}:{PORT}")
    app.run(host=HOST, port=PORT, debug=False, threaded=True)

if __name__ == "__main__":
    start_server()