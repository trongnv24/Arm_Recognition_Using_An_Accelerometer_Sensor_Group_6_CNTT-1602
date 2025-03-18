import numpy as np
import pandas as pd
from tensorflow.keras.models import load_model
import joblib
import os

# ===== CẤU HÌNH ĐƯỜNG DẪN (BẠN CÓ THỂ CHỈNH SỬA Ở ĐÂY) =====
# Đường dẫn đến các file mô hình, scaler, encoder
MODEL_PATH = "LSTM4/lstm_gesture_model3.h5"
SCALER_PATH = "LSTM4/scaler3.pkl"
LABEL_ENCODER_PATH = "LSTM4/label_encoder3.pkl"

# Đường dẫn đến file CSV chứa dữ liệu mới cần dự đoán
NEW_DATA_CSV = "nhan2_1.csv"  # Thay đổi thành tên file của bạn

# Số bước thời gian cho mỗi chuỗi đầu vào (phải giống với giá trị khi huấn luyện)
TIME_STEPS = 50

def predict(model_path, scaler_path, label_encoder_path, new_data_csv, time_steps=50):
    """
    Dự đoán cử chỉ từ dữ liệu mới trong file CSV
    """
    print(f"Đang tải mô hình từ {model_path}...")
    model = load_model(model_path)
    
    print(f"Đang tải scaler từ {scaler_path}...")
    scaler = joblib.load(scaler_path)
    
    print(f"Đang tải label encoder từ {label_encoder_path}...")
    le = joblib.load(label_encoder_path)
    
    print(f"Đang đọc dữ liệu mới từ {new_data_csv}...")
    try:
        new_data = pd.read_csv(new_data_csv)
        # Kiểm tra xem file CSV có các cột cần thiết không
        required_columns = ['AccX', 'AccY', 'AccZ', 'GyroX', 'GyroY', 'GyroZ']
        missing_columns = [col for col in required_columns if col not in new_data.columns]
        
        if missing_columns:
            print(f"Lỗi: Thiếu các cột sau trong file CSV: {missing_columns}")
            return None
        
        print(f"Đã đọc được {len(new_data)} mẫu dữ liệu.")
    except Exception as e:
        print(f"Lỗi khi đọc file CSV: {e}")
        return None

    # Chỉ lấy các cột cần thiết
    features = new_data[['AccX', 'AccY', 'AccZ', 'GyroX', 'GyroY', 'GyroZ']].values
    
    # Kiểm tra có đủ dữ liệu không
    if len(features) < time_steps:
        print(f"Lỗi: Cần ít nhất {time_steps} mẫu để dự đoán, nhưng chỉ có {len(features)} mẫu.")
        return None
    
    # Chuẩn hóa dữ liệu
    print("Đang chuẩn hóa dữ liệu...")
    features_scaled = scaler.transform(features)
    
    # Tạo các chuỗi thời gian để dự đoán
    sequences = []
    for i in range(len(features_scaled) - time_steps + 1):
        sequences.append(features_scaled[i:i + time_steps])
    
    sequences = np.array(sequences)
    print(f"Đã tạo {len(sequences)} chuỗi thời gian để dự đoán.")
    
    # Dự đoán
    print("Đang thực hiện dự đoán...")
    predictions = model.predict(sequences)
    
    # Lấy nhãn và độ tin cậy
    predicted_indices = np.argmax(predictions, axis=1)
    predicted_labels = le.inverse_transform(predicted_indices)
    confidences = np.max(predictions, axis=1) * 100  # Chuyển về phần trăm
    
    # Tạo DataFrame kết quả
    results = pd.DataFrame({
        'Dự đoán': predicted_labels,
        'Độ tin cậy (%)': confidences.round(2)
    })
    
    # Tính số lượng và tỷ lệ của mỗi cử chỉ được dự đoán
    gesture_counts = results['Dự đoán'].value_counts().sort_index()
    total_predictions = len(results)
    gesture_percentages = (gesture_counts / total_predictions * 100).round(2)
    
    # Kết luận về cử chỉ của toàn bộ dữ liệu dựa trên cử chỉ phổ biến nhất
    most_common_gesture = gesture_counts.idxmax()
    most_common_percentage = gesture_percentages[most_common_gesture]
    
    print("\n===== KẾT QUẢ DỰ ĐOÁN =====")
    print(f"Tổng số dự đoán: {total_predictions}")
    print("\nSố lượng mỗi cử chỉ:")
    for gesture, count in gesture_counts.items():
        print(f"Cử chỉ {gesture}: {count} mẫu ({gesture_percentages[gesture]}%)")
    
    print(f"\nKết luận: Dữ liệu được phân loại là CỬ CHỈ {most_common_gesture} với {most_common_percentage}% tỷ lệ xuất hiện")
    
    
    return most_common_gesture, most_common_percentage

if __name__ == "__main__":
    # Kiểm tra xem file có tồn tại không
    if not os.path.exists(NEW_DATA_CSV):
        print(f"Lỗi: File {NEW_DATA_CSV} không tồn tại!")
        input("Nhấn Enter để thoát...")
        exit(1)
    
    # Thực hiện dự đoán
    result = predict(
        model_path=MODEL_PATH,
        scaler_path=SCALER_PATH,
        label_encoder_path=LABEL_ENCODER_PATH,
        new_data_csv=NEW_DATA_CSV,
        time_steps=TIME_STEPS
    )
    
    if result:
        gesture, confidence = result
        print(f"\nDữ liệu của bạn là CỬ CHỈ {gesture} với độ tin cậy {confidence}%")
    
    # Giữ cửa sổ terminal mở
    input("\nNhấn Enter để thoát...")