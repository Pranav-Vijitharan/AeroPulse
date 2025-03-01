from flask import Flask, request, jsonify
import numpy as np
from pydub import AudioSegment
from scipy.signal import spectrogram
import tensorflow as tf
from tensorflow.keras.models import Model
from tensorflow.keras.layers import Conv1D, MaxPooling1D, BatchNormalization, GRU, add, Dense, LeakyReLU, Input
import os
from datetime import datetime
os.environ["CUDA_VISIBLE_DEVICES"] = "-1"

app = Flask(__name__)

# Ensure the uploads directory exists
UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Global variable to store the latest prediction
latest_prediction = {
    "prediction": None,
    "timestamp": None
}

@app.route('/upload', methods=['POST'])
def upload_file():
    global latest_prediction
    
    if 'file' not in request.files:
        return jsonify({'error': 'No file uploaded'}), 400

    file = request.files['file']
    if file.filename.split('.')[-1].lower() != 'wav':
        return jsonify({'error': 'Invalid file type. Only .wav files are allowed'}), 400

    file_path = os.path.join(UPLOAD_FOLDER, file.filename)
    file.save(file_path)

    classes = ["COPD", "Bronchiolitis", "Pneumonia", "URTI", "Healthy"]
    features = 52
    Input_Sample = Input(shape=(1, features))

    model_conv = Conv1D(256, kernel_size=5, strides=1, padding='same', activation='relu')(Input_Sample)
    model_conv = MaxPooling1D(pool_size=2, strides=2, padding='same')(model_conv)
    model_conv = BatchNormalization()(model_conv)

    model_conv = Conv1D(512, kernel_size=5, strides=1, padding='same', activation='relu')(model_conv)
    model_conv = MaxPooling1D(pool_size=2, strides=2, padding='same')(model_conv)
    model_conv = BatchNormalization()(model_conv)

    model_2_1 = GRU(32, return_sequences=True, activation='tanh', go_backwards=True)(model_conv)
    model_2 = GRU(128, return_sequences=True, activation='tanh', go_backwards=True)(model_2_1)
    model_3 = GRU(64, return_sequences=True, activation='tanh', go_backwards=True)(model_conv)
    model_3 = GRU(128, return_sequences=True, activation='tanh', go_backwards=True)(model_3)
    model_x = GRU(64, return_sequences=True, activation='tanh', go_backwards=True)(model_conv)
    model_x = GRU(128, return_sequences=True, activation='tanh', go_backwards=True)(model_x)

    model_add_1 = add([model_3, model_2, model_x])
    model_5 = GRU(128, return_sequences=True, activation='tanh', go_backwards=True)(model_add_1)
    model_5 = GRU(32, return_sequences=True, activation='tanh', go_backwards=True)(model_5)
    model_6 = GRU(64, return_sequences=True, activation='tanh', go_backwards=True)(model_add_1)
    model_6 = GRU(32, return_sequences=True, activation='tanh', go_backwards=True)(model_6)

    model_add_2 = add([model_5, model_6, model_2_1])
    model_7 = Dense(32, activation=None)(model_add_2)
    model_7 = LeakyReLU()(model_7)
    model_7 = Dense(128, activation=None)(model_7)
    model_7 = LeakyReLU()(model_7)
    model_9 = Dense(64, activation=None)(model_add_2)
    model_9 = LeakyReLU()(model_9)
    model_9 = Dense(128, activation=None)(model_9)
    model_9 = LeakyReLU()(model_9)
    model_add_3 = add([model_7, model_9])
    model_10 = Dense(64, activation=None)(model_add_3)
    model_10 = LeakyReLU()(model_10)
    model_10 = Dense(32, activation=None)(model_10)
    model_10 = LeakyReLU()(model_10)
    model_10 = Dense(5, activation="softmax")(model_10)

    gru_model = Model(inputs=Input_Sample, outputs=model_10)
    gru_model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.0001), loss='categorical_crossentropy', metrics=['accuracy'])

    try:
        gru_model.load_weights('GRU_Model.h5')
        print("Model weights loaded successfully!")
    except Exception as e:
        print("Error loading model weights:", e)

    audio = AudioSegment.from_file(file_path)
    audio_samples = np.array(audio.get_array_of_samples())
    sampling_rate = audio.frame_rate
    frequencies, times, spec = spectrogram(audio_samples, fs=sampling_rate)

    target_features = 52
    spectrogram_mean = np.mean(spec, axis=1)
    if len(spectrogram_mean) > target_features:
        spectrogram_mean = spectrogram_mean[:target_features]
    else:
        spectrogram_mean = np.pad(spectrogram_mean, (0, target_features - len(spectrogram_mean)), mode='constant')

    val = np.expand_dims([spectrogram_mean], axis=1)
    prediction = classes[np.argmax(gru_model.predict(val))]
    print(f"Prediction: {prediction}")

    latest_prediction = {
        "prediction": prediction,
        "timestamp": datetime.now().isoformat()
    }

    return jsonify({'message': f"Prediction: {prediction}"}), 200


@app.route('/get_prediction', methods=['GET'])
def get_prediction():
    # Return the latest prediction
    if latest_prediction["prediction"] is None:
        return jsonify({
            'message': 'No prediction available yet',
            'status': 'error'
        }), 404
    return jsonify(latest_prediction), 200

@app.route('/', methods=['GET'])
def home():
    return jsonify({
        'message': 'Welcome to the AeroPulse API!',
        'status': 'API is running',
        'usage': {
            'POST /upload': 'Upload a .wav file for processing',
            'GET /get_prediction': 'Get the latest prediction result',
            'GET /': 'Get API status and usage information'
        }
    }), 200

if __name__ == '__main__':
    app.run(debug=True, port=8081)
