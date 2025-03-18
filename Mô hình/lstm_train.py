import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler, LabelEncoder
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense, Dropout
from tensorflow.keras.utils import to_categorical
from tensorflow.keras.callbacks import EarlyStopping, ModelCheckpoint
import matplotlib.pyplot as plt
from sklearn.metrics import confusion_matrix, classification_report
import seaborn as sns

# 1. Đọc dữ liệu
print("Đang đọc dữ liệu...")
data = pd.read_csv('LSTM4/dulieu1.csv')
print(f"Dữ liệu có {data.shape[0]} dòng và {data.shape[1]} cột")
print(data.head())

# 2. Tiền xử lý dữ liệu
# Kiểm tra giá trị null
print("\nKiểm tra giá trị null:")
print(data.isnull().sum())

# Phân tách các đặc trưng và nhãn
X = data.iloc[:, :-1].values  # Tất cả cột trừ cột cuối
y = data.iloc[:, -1].values   # Cột cuối là nhãn Gesture

# Chuẩn hóa các đặc trưng
scaler = StandardScaler()
X = scaler.fit_transform(X)

# Mã hóa nhãn
le = LabelEncoder()
y = le.fit_transform(y)  # Chuyển từ 1,2,3,4 thành 0,1,2,3
y_categorical = to_categorical(y)

print(f"\nSố lượng lớp: {len(np.unique(y))}")
print(f"Các lớp sau khi chuyển đổi: {np.unique(y)}")

# 3. Chuẩn bị dữ liệu cho LSTM
# LSTM cần dữ liệu định dạng 3D: [samples, time steps, features]
# Chúng ta sẽ sử dụng một cửa sổ trượt để tạo chuỗi dữ liệu
def create_sequences(X, y, time_steps=50, step=10):
    Xs, ys = [], []
    for i in range(0, len(X) - time_steps, step):
        Xs.append(X[i:(i + time_steps)])
        ys.append(y[i + time_steps - 1])  # Lấy nhãn của mẫu cuối cùng trong chuỗi
    return np.array(Xs), np.array(ys)

# Tham số cho việc tạo chuỗi
TIME_STEPS = 50  # Độ dài của chuỗi thời gian
STEP = 10        # Bước nhảy giữa các chuỗi

# Tạo chuỗi
print("\nĐang tạo chuỗi dữ liệu cho LSTM...")
X_seq, y_seq = create_sequences(X, y_categorical, TIME_STEPS, STEP)
print(f"Dữ liệu sau khi tạo chuỗi: X shape: {X_seq.shape}, y shape: {y_seq.shape}")

# 4. Chia dữ liệu thành tập huấn luyện và tập kiểm tra
X_train, X_test, y_train, y_test = train_test_split(X_seq, y_seq, test_size=0.2, random_state=42)
print(f"\nSố lượng mẫu huấn luyện: {X_train.shape[0]}")
print(f"Số lượng mẫu kiểm tra: {X_test.shape[0]}")

# 5. Xây dựng mô hình LSTM
print("\nXây dựng mô hình LSTM...")
model = Sequential([
    LSTM(100, return_sequences=True, input_shape=(TIME_STEPS, X.shape[1])),
    Dropout(0.2),
    LSTM(100),
    Dropout(0.2),
    Dense(50, activation='relu'),
    Dropout(0.2),
    Dense(y_categorical.shape[1], activation='softmax')
])

# Biên dịch mô hình
model.compile(loss='categorical_crossentropy', optimizer='adam', metrics=['accuracy'])
model.summary()

# 6. Huấn luyện mô hình
# Định nghĩa callbacks
early_stopping = EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True)
model_checkpoint = ModelCheckpoint('best_lstm_model.h5', monitor='val_accuracy', 
                                   save_best_only=True, verbose=1)

# Huấn luyện mô hình
print("\nĐang huấn luyện mô hình...")
history = model.fit(
    X_train, y_train,
    epochs=50,
    batch_size=64,
    validation_split=0.2,
    callbacks=[early_stopping, model_checkpoint],
    verbose=1
)

# 7. Đánh giá mô hình
print("\nĐánh giá mô hình trên tập kiểm tra...")
loss, accuracy = model.evaluate(X_test, y_test)
print(f"Độ chính xác trên tập kiểm tra: {accuracy:.4f}")

# Dự đoán trên tập kiểm tra
y_pred = model.predict(X_test)
y_pred_classes = np.argmax(y_pred, axis=1)
y_test_classes = np.argmax(y_test, axis=1)

# 8. Hiển thị các đánh giá chi tiết
# Confusion matrix
print("\nConfusion Matrix:")
cm = confusion_matrix(y_test_classes, y_pred_classes)
print(cm)

# Báo cáo phân loại
print("\nClassification Report:")
cr = classification_report(y_test_classes, y_pred_classes, 
                           target_names=[f'Gesture {i+1}' for i in range(len(np.unique(y)))])
print(cr)

# 9. Lưu mô hình và scaler để sử dụng sau này
print("\nLưu mô hình và scaler...")
model.save('lstm_gesture_model.h5')
import joblib
joblib.dump(scaler, 'scaler.pkl')
joblib.dump(le, 'label_encoder.pkl')

# 10. Trực quan hóa quá trình huấn luyện
plt.figure(figsize=(12, 4))

# Đồ thị độ chính xác
plt.subplot(1, 2, 1)
plt.plot(history.history['accuracy'])
plt.plot(history.history['val_accuracy'])
plt.title('Độ chính xác của mô hình')
plt.ylabel('Accuracy')
plt.xlabel('Epoch')
plt.legend(['Train', 'Validation'], loc='lower right')

# Đồ thị loss
plt.subplot(1, 2, 2)
plt.plot(history.history['loss'])
plt.plot(history.history['val_loss'])
plt.title('Loss của mô hình')
plt.ylabel('Loss')
plt.xlabel('Epoch')
plt.legend(['Train', 'Validation'], loc='upper right')

plt.tight_layout()
plt.savefig('training_history2.png')
plt.show()

# 11. Trực quan hóa confusion matrix
plt.figure(figsize=(8, 6))
sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
            xticklabels=[f'Gesture {i+1}' for i in range(len(np.unique(y)))],
            yticklabels=[f'Gesture {i+1}' for i in range(len(np.unique(y)))])
plt.title('Confusion Matrix')
plt.ylabel('Nhãn thực tế')
plt.xlabel('Nhãn dự đoán')
plt.tight_layout()
plt.savefig('confusion_matrix2.png')
plt.show()

# 12. Hàm dự đoán cử chỉ với dữ liệu mới
def predict_gesture(new_data, time_steps=TIME_STEPS):
    """
    Dự đoán cử chỉ từ dữ liệu cảm biến mới
    
    new_data: numpy array với shape (n_samples, 6) chứa các đặc trưng AccX, AccY, AccZ, GyroX, GyroY, GyroZ
    time_steps: số lượng mẫu cần thiết cho một chuỗi thời gian (phải bằng với độ dài đã sử dụng khi huấn luyện)
    
    Trả về: nhãn cử chỉ được dự đoán (1, 2, 3, 4)
    """
    if len(new_data) < time_steps:
        raise ValueError(f"Cần ít nhất {time_steps} mẫu để dự đoán")
    
    # Chuẩn hóa dữ liệu mới
    new_data_scaled = scaler.transform(new_data)
    
    # Tạo chuỗi thời gian
    sequence = np.array([new_data_scaled[-time_steps:]])
    
    # Dự đoán
    prediction = model.predict(sequence)
    gesture_idx = np.argmax(prediction[0])
    
    # Chuyển từ index (0, 1, 2, 3) về nhãn gốc (1, 2, 3, 4)
    return le.inverse_transform([gesture_idx])[0]

print("\nKết thúc quá trình xây dựng và đánh giá mô hình.")