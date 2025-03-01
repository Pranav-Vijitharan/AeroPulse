# AeroPulse: AI-Enabled Stethoscope for Early Lung Disease Detection

AeroPulse is an innovative, AI-powered diagnostic tool designed to transform healthcare accessibility in developing countries. By converting breathing sounds into digital features and analyzing them with deep learning models, AeroPulse empowers healthcare providers with early, accurate diagnosis of lung diseases such as COPD, pneumonia, and bronchitis. Our solution leverages ubiquitous hardware—a stethoscope—to bring advanced diagnostic capabilities where traditional, complex medical devices are scarce.

---

## Problem Statement & Our Solution

### The Problem:
- **Lack of Accessible Medical Equipment:** In many developing countries, up to 70% of essential medical devices fail to work due to poor infrastructure, lack of training, and inadequate maintenance.
- **Early Detection Gap:** Limited access to diagnostic tools like spirometers and pulse oximeters results in delayed diagnosis and treatment of lung diseases.
- **High Healthcare Costs:** Misdiagnosis and late intervention lead to severe cases that burden the healthcare system and incur higher treatment costs.

### How AeroPulse Solves It:
- **AI-Driven Diagnostics:** Uses AI to analyze breathing sounds captured by a standard stethoscope, detecting early signs of lung diseases.
- **Low-Cost & Ubiquitous:** Leverages widely available stethoscopes, augmented with an ESP32-based hardware module, keeping costs low.
- **Remote & Proactive:** Integrates with mobile and wearable technologies to enable real-time health monitoring and early intervention.
- **Data Augmentation & Robust Models:** Uses advanced data augmentation techniques (e.g., adding noise, shifting, stretching, pitch shifting) during model training to improve robustness and accuracy.

---

## Project Structure

### 1. **model-training.ipynb**
- **Purpose:**  
  This Jupyter Notebook is dedicated to building, training, and evaluating the deep learning model. It:
  - Processes respiratory sound data.
  - Extracts features using MFCCs (Mel-Frequency Cepstral Coefficients) and mel spectrograms.
  - Implements data augmentation to simulate variations in breathing sounds.
  - Constructs a hybrid CNN-GRU network architecture to classify different lung conditions.
  - Saves the trained model weights for later deployment. (weights were hosted in the cloud)
- **Key Components:**
  - **Feature Extraction:** Code to extract MFCCs and spectrogram features.
  - **Data Augmentation:** Techniques to expand the training dataset.
  - **Model Architecture:** A multi-branch network combining convolutional and recurrent layers.
  - **Evaluation:** Performance metrics and visualizations to assess model accuracy. Model is able to hit 90% accuracy.
    <img width="467" alt="image" src="https://github.com/user-attachments/assets/3fa60099-9633-4070-83e9-b08630a7e568" />
    
    <img width="405" alt="image" src="https://github.com/user-attachments/assets/609abdac-de7e-485c-9938-4e68578c045d" />


### 2. **hardware.ino**
- **Purpose:**  
  This Arduino sketch is designed for the ESP32-based hardware prototype. It handles:
  - **Audio Capture:** Configures the I2S peripheral to record respiratory sounds.
  - **Data Storage:** Uses SPIFFS to store the recording as a WAV file.
  - **User Interface:** Manages user interactions with an OLED display and a buzzer.
  - **Network Communication:** Connects to Wi-Fi and uploads recorded audio to the server for AI processing.
- **Key Functions:**
  - **Audio Recording & Processing:** Initialization of I2S and SPIFFS, handling recording buffers, and writing a proper WAV header.
  - **User Feedback:** Animations and audio cues (chimes, countdowns) displayed on an OLED.
  - **File Upload:** Sends recorded WAV files in chunks to the server via HTTP.

### 3. **app.py**
- **Purpose:**  
  The Flask application acts as the backend API for AeroPulse. This is being hosted in the cloud. We are using Render. It:
  - **Receives Audio Files:** Provides an endpoint (`/upload`) to accept WAV files uploaded from the ESP32 device.
  - **Preprocesses Audio Data:** Extracts audio features (MFCCs) from the uploaded file.
  - **Predicts the Condition:** Loads the pre-trained GRU model and predicts lung disease from the audio features.
  - **Serves Prediction Results:** Offers another endpoint (`/get_prediction`) for retrieving the latest prediction.
- **Key Endpoints:**
  - **`POST /upload`:** Handles file upload, feature extraction, and prediction.
  - **`GET /get_prediction`:** Returns the most recent prediction and timestamp.
  - **`GET /`:** Provides basic API status and usage information.

---

## How It Works: End-to-End Flow

1. **Data Capture (hardware.ino):**
   - The user presses a button on the device.
   - The ESP32 records a 10-second audio sample using the I2S interface.
   - The recording is saved as a WAV file in SPIFFS.
   - The file is then uploaded to the AeroPulse server via an HTTP POST request.

2. **Data Processing & Prediction (app.py):**
   - The server receives the WAV file at the `/upload` endpoint.
   - Audio features are extracted using `librosa.feature.mfcc()`, ensuring consistency with the training data.
   - A pre-trained deep learning model (loaded from `GRU_Model.h5`) predicts the lung condition.
   - The prediction, along with a timestamp, is stored and made available through `/get_prediction`.

3. **Feedback & Display (hardware.ino):**
   - The ESP32 periodically polls the server for prediction results.
   - The prediction result is displayed on the OLED screen, and corresponding audio feedback (chimes) is played.

---

## Conclusion

AeroPulse is designed to empower underserved communities by providing a cost-effective, easy-to-use diagnostic tool that leverages AI to detect lung diseases early. By transforming a standard stethoscope into an intelligent device, AeroPulse can significantly reduce the burden on healthcare systems, enabling timely treatment and improving patient outcomes.

For any further details or queries regarding our approach, implementation, or future improvements, please refer to the code files or contact our team.

---
