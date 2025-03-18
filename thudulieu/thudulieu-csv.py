from flask import Flask, request, jsonify
import csv
import os

app = Flask(__name__)

CSV_FILE = "nhan2_1.csv"

if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, mode="w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["AccX", "AccY", "AccZ", "GyroX", "GyroY", "GyroZ"])

@app.route("/", methods=["POST"])
def receive_data():
    try:
        data = request.get_json()
        if not data:
            return jsonify({"error": "No data received"}), 400

        with open(CSV_FILE, mode="a", newline="") as file:
            writer = csv.writer(file)
            for record in data:
                writer.writerow([
                    record["AccX"], record["AccY"], record["AccZ"], 
                    record["GyroX"], record["GyroY"], record["GyroZ"]
                ])

        print(f"Saved {len(data)} records")
        return jsonify({"message": f"Saved {len(data)} records"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 400

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
