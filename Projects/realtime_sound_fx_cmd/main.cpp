// Either open as a standalone workspace or add the project folder to Workspace (File -> Add Folder to Workspace)
// Don't use Code Runner to compile, use stardard "Run C/C++ File" because of custom workspace's "tasks.json"
// Code Runner wont link custom libraries like portaudio

#include <iostream>
#include <portaudio.h>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512

float volume_amplifier = 1.0f;
bool voice_clipping_protection = true;

void VolumeBoost() {
    system("cls");
    std::cout << "\nInput number(can be float):\n";
    std::cout << "0 - mute\n";
    std::cout << "1 - normal\n";
    std::cout << "100 - high gain\n";
    float ans;
    std::cin >> ans;
    volume_amplifier = ans;
    return;
}

void VoiceClippingProtection() {
    system("cls");
    std::cout << "\nVoice clipping Protection is ";
    if(voice_clipping_protection == true) {
        std::cout << "ON\nDisable? [Y/n]\n";
    }
    else {
        std::cout << "OFF\nEnable? [Y/n]\n";
    }
    char ans;
    std::cin >> ans;
    if(ans != 'n') {
        if(voice_clipping_protection == true) {
            std::cout << "\nDISABLED Voice Clipping Protection\n";
            voice_clipping_protection = false;
        }
        else {
            std::cout << "\nENABLED Voice Clipping Protection\n";
            voice_clipping_protection = true;
        }
        std::cout << "Settings applied.\n";
    }
    return;
}

int audioCallback(const void* inputBuffer, void* outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void* userData) {
    const float* in = (const float*)inputBuffer;
    float* out = (float*)outputBuffer;

    if(inputBuffer == nullptr) {
        // Silence output if no input
        for(unsigned int i = 0; i < framesPerBuffer; i++) {
            *out++ = 0.0f;
        }
        return paContinue;
    }

    for(unsigned int i = 0; i < framesPerBuffer; i++) {
        float sample = *in++ * volume_amplifier; // reduce volume to avoid clipping, otherwise it will be distortion

        // Clipping protection
        if(voice_clipping_protection == true) {
            if(sample > 1.0f) sample = 1.0f;
            if(sample < -1.0f) sample = -1.0f;
        }

        *out++ = sample;
    }

    return paContinue;
}

int main() {
    PaError err;

    err = Pa_Initialize();
    if(err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    // list all devices
    /*
    for (int i = 0; i < Pa_GetDeviceCount(); i++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        std::cout << i << ": " << info->name << "\n";
    }
    */

    PaStreamParameters inputParams, outputParams;

    inputParams.device = Pa_GetDefaultInputDevice();

    for(int i = 0; i < Pa_GetDeviceCount(); ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if(info->maxOutputChannels > 0) {
            std::string name(info->name);
            if(name.find("Stream") != std::string::npos) {
                outputParams.device = i;
                outputParams.channelCount = std::min(2, info->maxOutputChannels); // usually stereo
                break;
            }
        }
    }

    // outputParams.device = Pa_GetDefaultOutputDevice();

    // std::cout<<"output DEVICE: "<<outputParams.device<<'\n';
    // std::cout<<"input: "<<inputParams.device<<'\n';

    if(inputParams.device == paNoDevice || outputParams.device == paNoDevice) {
        std::cout << "Error: No default input/output device.\n";
        Pa_Terminate();
        std::cin.get();
        return 1;
    }

    inputParams.channelCount = 1;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    outputParams.channelCount = 1;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream;

    err = Pa_OpenStream(&stream,
                        &inputParams,
                        &outputParams,
                        SAMPLE_RATE,
                        FRAMES_PER_BUFFER,
                        paClipOff, // we handle our own clipping
                        audioCallback,
                        nullptr);

    if(err != paNoError) {
        std::cout << "Failed to open stream: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        std::cin.get();
        return 1;
    }

    err = Pa_StartStream(stream);
    if(err != paNoError) {
        std::cout << "Failed to start stream: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        std::cin.get();
        return 1;
    }

    std::string q = "";
    while(q != "quit") {
        std::cout << "\nLive audio passthrough running...\nUncheck the 'add to stream mix' in Sonar's Mic Settings (click on the icon)\n\n";
        std::cout << "[quit] - Kill task\n";
        std::cout << "[1] - Volume Overdrive\n";
        std::cout << "[2] - Voice Clipping Protection\n";
        std::cin >> q;
        if(q == "1") {
            VolumeBoost();
            std::cout << "Settings applied.\n";
        }
        if(q == "2") {
            VoiceClippingProtection();
        }
        else {
            system("cls");
        }
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    return 0;
}
