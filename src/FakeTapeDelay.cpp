#include "plugin.hpp"

#define DELAY_PARAM_MIN 0.1f
#define DELAY_PARAM_MAX 2.0f
#define DELAY_PARAM_DEFAULT 1.0f

struct FakeTapeDelay : Module {
    enum ParamId {
        SPEED_PARAM,
        FEEDBACK_PARAM,
        SPEED_CV_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_INPUT,
        SPEED_INPUT,
        RETURN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_OUTPUT,
        SEND_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    std::vector<float> delay_buffer;
    float tp = 0;
    float last_delay_line_input_mix = 0;
    float delay_time_ms = 100.f;
    int delay_samples = 4800;

    FakeTapeDelay() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(SPEED_PARAM, DELAY_PARAM_MIN, DELAY_PARAM_MAX, DELAY_PARAM_DEFAULT, "Playback Speed");
        configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.7f, "Feedback");
        configParam(SPEED_CV_PARAM, 0.f, 1.f, 1.f, "Speed Input CV");
        configInput(IN_INPUT, "Sound Input");
        configInput(RETURN_INPUT, "Insert Return Input");
        configInput(SPEED_INPUT, "Playback Speed Input");
        configOutput(OUT_OUTPUT, "Delay Output");
        configOutput(SEND_OUTPUT, "Insert Send Output");

        update_delay_buffer();
    }

    void update_delay_buffer() {
        delay_samples = (int)(delay_time_ms / 1000.f * APP->engine->getSampleRate());
        delay_buffer.resize(0, 0.f);
        delay_buffer.resize(delay_samples, 0.f);
        tp = 0;
    }


    float state[4] = {};

    float processFilter(float input, float sampleRate, float cutoff) {
        // Calculate cutoff frequency in Hz
        float fc = cutoff * sampleRate * 0.5f;

        // Calculate the filter coefficient
        float RC = 1.0f / (2.0f * M_PI * fc);
        float dt = 1.0f / sampleRate;
        float alpha = dt / (RC + dt);

        // 4-pole filter: cascade of four first-order filters
        float x = input;
        for (int i = 0; i < 4; ++i) {
            state[i] = alpha * x + (1.0f - alpha) * state[i];
            x = state[i];
        }

        return state[3];
    }


    void process(const ProcessArgs &args) override {
        float playback_speed = params[SPEED_PARAM].getValue();
        if (inputs[SPEED_INPUT].isConnected()) {
            playback_speed += inputs[SPEED_INPUT].getVoltage() * params[SPEED_CV_PARAM].getValue() / 10.f;
            playback_speed = clamp(playback_speed, DELAY_PARAM_MIN, DELAY_PARAM_MAX);
        }

        float feedback = params[FEEDBACK_PARAM].getValue();
        float raw_input_signal = inputs[IN_INPUT].isConnected() ? inputs[IN_INPUT].getVoltage() : 0.f;
        float input_signal = processFilter(raw_input_signal, args.sampleRate, playback_speed < 1.0 ? playback_speed : 1.0);


        // Read from the delay line
        float rp = fmodf(tp + 50, delay_samples);
        int irp = (int)rp;
        int irp1 = (irp + 1) % delay_samples;
        float rpf = rp - irp;
        float delayed_signal = (1.f - rpf) * delay_buffer[irp] + rpf * delay_buffer[irp1];

        outputs[SEND_OUTPUT].setVoltage(delayed_signal);
        if (inputs[RETURN_INPUT].isConnected()) {
            delayed_signal = inputs[RETURN_INPUT].getVoltage();
        }

        // Create delay-line input mix
        float delay_line_input_mix = input_signal + feedback * delayed_signal;

        // Write to a delay-line (if neccessary)
        float new_tp = tp + playback_speed;
        int itp = (int)tp;
        int new_itp = (int)new_tp;
        for (int ip = itp + 1; ip <= new_itp; ip++) {
            float p = (ip - tp) / playback_speed;
            float write_to_buffer = last_delay_line_input_mix * (1 - p) + delay_line_input_mix * p;
            delay_buffer[ip % delay_samples] = write_to_buffer;
        }

        outputs[OUT_OUTPUT].setVoltage(delayed_signal);
        last_delay_line_input_mix = delay_line_input_mix;
        tp = fmodf(new_tp, delay_samples);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "delay_time_ms", json_real(delay_time_ms));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* delayTimeJ = json_object_get(rootJ, "delay_time_ms");
        if (delayTimeJ) {
            delay_time_ms = json_real_value(delayTimeJ);
            update_delay_buffer();
        }
    }
};

struct DelayBufferMenuItem : MenuItem {
    FakeTapeDelay* module;
    float delay_time_ms;

    void onAction(const event::Action& e) override {
        if (module) {
            module->delay_time_ms = delay_time_ms;
            module->update_delay_buffer();
        }
    }

    void step() override {
        rightText = CHECKMARK(module && module->delay_time_ms == delay_time_ms);
    }
};

struct FakeTapeDelayWidget : ModuleWidget {
    FakeTapeDelayWidget(FakeTapeDelay* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FakeTapeDelay.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(15.199, 29.989)), module, FakeTapeDelay::SPEED_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15.247, 51.618)), module, FakeTapeDelay::FEEDBACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(8.04, 68.226)), module, FakeTapeDelay::SPEED_CV_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.425, 68.1)), module, FakeTapeDelay::SPEED_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.333, 88.117)), module, FakeTapeDelay::IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.112, 88.396)), module, FakeTapeDelay::RETURN_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.189, 109.457)), module, FakeTapeDelay::SEND_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.512, 109.6)), module, FakeTapeDelay::OUT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        FakeTapeDelay* module = dynamic_cast<FakeTapeDelay*>(this->module);
        if (!module)
            return;

        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuLabel("Delay Buffer Size"));

        const std::vector<std::pair<float, std::string>> options = {
            {30.f, "30 ms"},
            {100.f, "100 ms"},
            {300.f, "300 ms"},
            {1000.f, "1 sec"}
        };

        for (const auto& option : options) {
            DelayBufferMenuItem* item = new DelayBufferMenuItem();
            item->text = option.second;
            item->module = module;
            item->delay_time_ms = option.first;
            menu->addChild(item);
        }
    }
};


Model* modelFakeTapeDelay = createModel<FakeTapeDelay, FakeTapeDelayWidget>("FakeTapeDelay");
