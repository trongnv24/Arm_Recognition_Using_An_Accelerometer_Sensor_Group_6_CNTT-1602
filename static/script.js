// Cấu hình biểu đồ
const MAX_DATA_POINTS = 75;  // Số điểm dữ liệu tối đa hiển thị trên biểu đồ
const UPDATE_INTERVAL = 100; // Cập nhật dữ liệu mỗi 100ms

// Khởi tạo biểu đồ
const ctx = document.getElementById('mpu-chart').getContext('2d');
const initialData = Array(MAX_DATA_POINTS).fill(null);

const chart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: Array(MAX_DATA_POINTS).fill(''),
        datasets: [
            {
                label: 'X',
                data: [...initialData],
                borderColor: 'red',
                tension: 0.3,
                borderWidth: 2,
                fill: false,
                pointRadius: 0
            },
            {
                label: 'Y',
                data: [...initialData],
                borderColor: 'green',
                tension: 0.3,
                borderWidth: 2,
                fill: false,
                pointRadius: 0
            },
            {
                label: 'Z',
                data: [...initialData],
                borderColor: 'blue',
                tension: 0.3,
                borderWidth: 2,
                fill: false,
                pointRadius: 0
            }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            y: {
                beginAtZero: false,
                suggestedMin: -2,
                suggestedMax: 2,
                grid: {
                    color: 'rgba(0, 0, 0, 0.1)'
                }
            },
            x: {
                grid: {
                    display: false
                }
            }
        },
        animation: {
            duration: 0  // Tắt animation để tăng hiệu suất
        },
        plugins: {
            legend: {
                display: false
            },
            tooltip: {
                enabled: false  // Tắt tooltip để tăng hiệu suất
            }
        },
        elements: {
            line: {
                cubicInterpolationMode: 'monotone'  // Làm mịn đường
            }
        }
    }
});

// Định dạng số thập phân
function formatNumber(number, decimals = 3) {
    return Number(number).toFixed(decimals);
}

// Cập nhật dữ liệu
function updateData() {
    // Lấy thông tin dự đoán
    fetch('/prediction')
        .then(response => response.json())
        .then(data => {
            // Cập nhật thông tin dự đoán
            document.getElementById('current-gesture').textContent = data.gesture;
            document.getElementById('confidence').textContent = formatNumber(data.confidence, 2) + '%';
            document.getElementById('buffer-size').textContent = data.buffer_size;
            document.getElementById('samples-received').textContent = data.samples_received;
            document.getElementById('time').textContent = data.time;
            
            // Cập nhật trạng thái thu thập
            const statusElement = document.getElementById('collection-status');
            if (data.is_collecting) {
                statusElement.textContent = 'Đang thu thập dữ liệu...';
                statusElement.className = 'collection-status collecting';
                document.getElementById('startCollectionBtn').disabled = true;
                document.getElementById('stopCollectionBtn').disabled = false;
                
                // Hiển thị thời gian đã thu thập
                if (data.collection_start_time) {
                    const startTime = new Date(data.collection_start_time * 1000);
                    const currentTime = new Date();
                    const elapsedSeconds = Math.floor((currentTime - startTime) / 1000);
                    document.getElementById('collection-time').textContent = `Thời gian: ${elapsedSeconds} giây`;
                }
            } else {
                statusElement.textContent = 'Không thu thập dữ liệu';
                statusElement.className = 'collection-status not-collecting';
                document.getElementById('startCollectionBtn').disabled = false;
                document.getElementById('stopCollectionBtn').disabled = true;
                document.getElementById('collection-time').textContent = '';
            }
        })
        .catch(error => {
            console.error('Lỗi khi lấy thông tin dự đoán:', error);
        });
    
    // Lấy thống kê dự đoán
    fetch('/stats')
        .then(response => response.json())
        .then(data => {
            // Tạo bảng thống kê
            let statsHtml = '<table><tr><th>Nhãn</th><th>Số lần dự đoán</th></tr>';
            
            // Xác định tổng số lần dự đoán
            const totalPredictions = data.total_predictions || 0;
            
            // Thêm dữ liệu vào bảng
            for (const [label, count] of Object.entries(data.predictions)) {
                // Tính tỷ lệ phần trăm
                const percentage = totalPredictions > 0 ? (count / totalPredictions * 100).toFixed(1) : 0;
                statsHtml += `<tr><td>${label}</td><td>${count} (${percentage}%)</td></tr>`;
            }
            
            statsHtml += '</table>';
            document.getElementById('stats-data').innerHTML = statsHtml;
        })
        .catch(error => {
            console.error('Lỗi khi lấy thống kê dự đoán:', error);
            document.getElementById('stats-data').innerHTML = '<p style="color: red; font-weight: bold;">Không thể tải thống kê</p>';
        });
    
    // Lấy dữ liệu cảm biến và cập nhật biểu đồ
    fetch('/current-data')
        .then(response => response.json())
        .then(data => {
            if (data.latest_data && data.latest_data.length > 0) {
                // Lấy mẫu mới nhất
                const latestSample = data.latest_data[data.latest_data.length - 1];
                
                // Cập nhật hiển thị dữ liệu cảm biến
                document.getElementById('current-data').innerHTML = `
                    <div>AccX: ${formatNumber(latestSample[0])}</div>
                    <div>AccY: ${formatNumber(latestSample[1])}</div>
                    <div>AccZ: ${formatNumber(latestSample[2])}</div>
                    <div>GyroX: ${formatNumber(latestSample[3])}</div>
                    <div>GyroY: ${formatNumber(latestSample[4])}</div>
                    <div>GyroZ: ${formatNumber(latestSample[5])}</div>
                `;
                
                // Cập nhật dữ liệu biểu đồ
                chart.data.datasets[0].data.push(latestSample[0]);
                chart.data.datasets[1].data.push(latestSample[1]);
                chart.data.datasets[2].data.push(latestSample[2]);
                
                // Giới hạn số lượng điểm dữ liệu
                if (chart.data.datasets[0].data.length > MAX_DATA_POINTS) {
                    chart.data.datasets[0].data.shift();
                    chart.data.datasets[1].data.shift();
                    chart.data.datasets[2].data.shift();
                }
                
                // Cập nhật biểu đồ
                chart.update();
            } else {
                document.getElementById('current-data').innerHTML = '<div>Chưa có dữ liệu</div>';
            }
        })
        .catch(error => {
            console.error('Lỗi khi lấy dữ liệu cảm biến:', error);
        });
}

// Gọi API để reset thống kê
function resetStats() {
    fetch('/reset', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        updateData();
    })
    .catch(error => {
        console.error('Lỗi khi reset thống kê:', error);
    });
}

// Gọi API để bắt đầu thu thập dữ liệu
function startCollection() {
    fetch('/start-collection', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        updateData();
    })
    .catch(error => {
        console.error('Lỗi khi bắt đầu thu thập:', error);
    });
}

// Gọi API để dừng thu thập và thực hiện dự đoán
function stopCollectionAndPredict() {
    fetch('/stop-collection-and-predict', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        updateData();
    })
    .catch(error => {
        console.error('Lỗi khi dừng và dự đoán:', error);
    });
}

// Gọi API để xóa buffer
function clearBuffer() {
    fetch('/clear-buffer', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        // Reset dữ liệu biểu đồ
        chart.data.datasets.forEach(dataset => {
            dataset.data = Array(MAX_DATA_POINTS).fill(null);
        });
        chart.update();
        updateData();
    })
    .catch(error => {
        console.error('Lỗi khi xóa buffer:', error);
    });
}

// Cập nhật dữ liệu định kỳ
setInterval(updateData, UPDATE_INTERVAL);

// Cập nhật dữ liệu lần đầu khi tải trang
document.addEventListener('DOMContentLoaded', updateData);