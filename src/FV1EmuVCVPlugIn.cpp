/* FV1 VCV PlugIn
 * Copyright (C)2018 - Eduard Heidt
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "FV1emu.hpp"
#include "plugin.hpp"
#include <osdialog.h>
#include <dirent.h>
#include <iterator>
#include <thread>

struct  FV1EmuModule : Module
{
	enum ParamIds
	{
		POT0_PARAM,
		POT1_PARAM,
		POT2_PARAM,
		TPOT0_PARAM,
		TPOT1_PARAM,
		TPOT2_PARAM,
		FX_PREV,
		FX_NEXT,
		DRYWET_PARAM,
		NUM_PARAMS
	};
	enum InputIds
	{
		POT_0,
		POT_1,
		POT_2,
		INPUT_L,
		INPUT_R,
		NUM_INPUTS
	};
	enum OutputIds
	{
		OUTPUT_L,
		OUTPUT_R,
		NUM_OUTPUTS
	};

	FV1emu fx;

	bool Debug = false;
	std::ifstream infilex;

	dsp::SchmittTrigger nextTrigger;
	dsp::SchmittTrigger prevTrigger;

	FV1EmuModule()
	{
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
		configParam(FX_PREV, 0.0f, 1.0f, 0.0f, "");
		configParam(FX_NEXT, 0.0f, 1.0f, 0.0f, "");
		configParam(POT0_PARAM, 0, 1.0, 0.0, "");
		configParam(POT1_PARAM, 0, 1.0, 0.0, "");
		configParam(POT2_PARAM, 0, 1.0, 0.0, "");
		configParam(TPOT0_PARAM, -1.0f, 1.0f, 0.0f, "");
		configParam(TPOT1_PARAM, -1.0f, 1.0f, 0.0f, "");
		configParam(TPOT2_PARAM, -1.0f, 1.0f, 0.0f, "");
		configParam(DRYWET_PARAM, -1.0f, 1.0f, 0.0f, "");

		loadFx(asset::plugin(pluginInstance, "fx/demo.spn"));
		INFO("FV1EmuModule()");
	}

	~FV1EmuModule()
	{
		INFO("~FV1EmuModule()");
	}

	void process(const ProcessArgs& args) override
	{
		if (filesInPath.size() > 0)
		{
			if (nextTrigger.process(params[FX_NEXT].getValue()))
			{
				auto it = std::find(filesInPath.cbegin(), filesInPath.cend(), lastPath);
				;
				if (it == filesInPath.cend() || ++it == filesInPath.cend())
					it = filesInPath.cbegin();

				loadFx(*it);
			}

			if (prevTrigger.process(params[FX_PREV].getValue()))
			{
				auto it = std::find(filesInPath.crbegin(), filesInPath.crend(), lastPath);

				if (it == filesInPath.crend() || ++it == filesInPath.crend())
					it = filesInPath.crbegin();

				loadFx(*it);
			}
		}

		//float deltaTime = args.sampleTime;
		auto inL = inputs[INPUT_L].getVoltage();
		auto inR = inputs[INPUT_R].getVoltage();
		auto outL = 0.0f;
		auto outR = 0.0f;

		auto p0 = params[POT0_PARAM].getValue();
		auto p1 = params[POT1_PARAM].getValue();
		auto p2 = params[POT2_PARAM].getValue();

		p0 += inputs[POT_0].getVoltage() * 0.1f * params[TPOT0_PARAM].getValue();
		p1 += inputs[POT_1].getVoltage() * 0.1f * params[TPOT1_PARAM].getValue();
		p2 += inputs[POT_2].getVoltage() * 0.1f * params[TPOT2_PARAM].getValue();

		float mix = params[DRYWET_PARAM].getValue();
		float d = clamp(1.f - mix, 0.0f, 1.0f);
		float w = clamp(1.f + mix, 0.0f, 1.0f);

		if (w > 0)
		{
			fx.run(inL * 0.1, inR * 0.1, p0, p1, p2, outL, outR);
			outL *= 10;
			outR *= 10;
		}

		outputs[OUTPUT_L].setVoltage(clamp(inputs[INPUT_L].getVoltage() * d + outL * w, -10.0f, 10.0f));
		outputs[OUTPUT_R].setVoltage(clamp(inputs[INPUT_R].getVoltage() * d + outR * w, -10.0f, 10.0f));
	}

	json_t *dataToJson() override
	{
		json_t *rootJ = json_object();

		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override
	{
		if (json_t *lastPathJ = json_object_get(rootJ, "lastPath"))
		{
			std::string file = json_string_value(lastPathJ);
			loadFx(file);
		}
	}

	std::string display;
	std::string lastPath;
	std::vector<std::string> filesInPath;

	void loadFx(const std::string &file)
	{
		this->lastPath = file;
		this->fx.load(file);

		filesInPath.clear();
		auto dir = string::directory(this->lastPath);
		if (auto rep = opendir(dir.c_str()))
		{
			while (auto dirp = readdir(rep))
			{
				std::string name = dirp->d_name;

				std::size_t found = name.find(".spn", name.length() - 5);
				if (found == std::string::npos)
					found = name.find(".spn", name.length() - 5);

				if (found != std::string::npos)
				{
#ifdef _WIN32
					filesInPath.push_back(dir + "\\" + name);
#else
					filesInPath.push_back(dir + "/" + name);
#endif
					INFO(name.c_str());
				}
			}

			closedir(rep);
		}

		std::sort(filesInPath.begin(), filesInPath.end());
		auto it = std::find(filesInPath.cbegin(), filesInPath.cend(), lastPath);
		auto fxIndex = it - filesInPath.cbegin();

		display = std::to_string(fxIndex) + ": " + this->fx.getDisplay();
	}
};

struct DisplayPanel : TransparentWidget
{
	const std::string &text;
	std::shared_ptr<Font> font;

	DisplayPanel(const Vec &pos, const Vec &size, const std::string &display) : text(display)
	{
		box.pos = pos;
		box.size = size;
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
	}

	void draw(NVGcontext *vg) override
	{
		nvgFontSize(vg, 12);
		nvgFillColor(vg, nvgRGBAf(1, 1, 1, 1));

		std::stringstream stream(text);
		std::string line;
		int y = 11;
		while (std::getline(stream, line))
		{
			nvgText(vg, 5, y, line.c_str(), NULL);

			if (y == 11)
				y += 5;
			y += 11;
		}
	}
};

struct DebugPanel : LedDisplayTextField
{
	FV1EmuModule *module;
	void onButton(const event::Button &e) override
	{
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0)
			module->Debug = false;
		LedDisplayTextField::onButton(e);
	}
};

struct PathMenuItem : MenuItem
{
	FV1EmuModule *module;
	void onAction(const event::Action &e) override
	{
		auto dir = module->lastPath.empty() ? asset::user("") : string::directory(module->lastPath);
		auto *filters = osdialog_filters_parse("FV1-FX Asm:spn");
		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
		if (path)
		{
			module->loadFx(path);
			free(path);
		}
	}
};

struct DebugMenuItem : MenuItem
{
	FV1EmuModule *module;
	void onAction(const event::Action &e) override
	{
		module->Debug = !module->Debug;
	}
	void step() override
	{
		rightText = ( module->Debug == true) ? "✔" : "";
		MenuItem::step();
	}
};

struct FV1EmuWidget : ModuleWidget
{
	DebugPanel *debugText;
	void appendContextMenu(Menu *menu) override
	{
		auto module = dynamic_cast<FV1EmuModule *>(this->module);
		assert(module);

		menu->addChild(new MenuEntry);

		PathMenuItem *pathItem = new PathMenuItem;
		pathItem->text = "Select Input File...";
		pathItem->module = module;
		menu->addChild(pathItem);

		DebugMenuItem *debugItem = new DebugMenuItem;
		debugItem->text = "DEBUG";
		debugItem->module = module;
		menu->addChild(debugItem);
	}

	FV1EmuWidget(FV1EmuModule *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/panel.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module)
		{
			auto display = new DisplayPanel(Vec(12, 31), Vec(100, 50), module->display);
			addChild(display);
		}

		addParam(createParam<TL1105>(Vec(105, 88), module, FV1EmuModule::FX_PREV));
		addParam(createParam<TL1105>(Vec(130, 88), module, FV1EmuModule::FX_NEXT));

		addParam(createParam<RoundLargeBlackKnob>(Vec(13, 115), module, FV1EmuModule::POT0_PARAM));
		addParam(createParam<RoundLargeBlackKnob>(Vec(64, 115), module, FV1EmuModule::POT1_PARAM));
		addParam(createParam<RoundLargeBlackKnob>(Vec(115, 115), module, FV1EmuModule::POT2_PARAM));

		addParam(createParam<Trimpot>(Vec(21, 169), module, FV1EmuModule::TPOT0_PARAM));
		addParam(createParam<Trimpot>(Vec(72, 169), module, FV1EmuModule::TPOT1_PARAM));
		addParam(createParam<Trimpot>(Vec(123, 169), module, FV1EmuModule::TPOT2_PARAM));

		addInput(createInput<PJ301MPort>(Vec(18, 202), module, FV1EmuModule::POT_0));
		addInput(createInput<PJ301MPort>(Vec(69, 202), module, FV1EmuModule::POT_1));
		addInput(createInput<PJ301MPort>(Vec(120, 202), module, FV1EmuModule::POT_2));

		addParam(createParam<RoundBlackKnob>(Vec(67, 235), module, FV1EmuModule::DRYWET_PARAM));

		addInput(createInput<PJ301MPort>(Vec(10, 280), module, FV1EmuModule::INPUT_L));
		addInput(createInput<PJ301MPort>(Vec(10, 320), module, FV1EmuModule::INPUT_R));

		addOutput(createOutput<PJ301MPort>(Vec(box.size.x - 30, 280), module, FV1EmuModule::OUTPUT_L));
		addOutput(createOutput<PJ301MPort>(Vec(box.size.x - 30, 320), module, FV1EmuModule::OUTPUT_R));

		debugText = new DebugPanel();
		debugText->module = module;
		debugText->box.size = box.size;
		debugText->multiline = true;
		debugText->visible = false;
		debugText->setText("Turn on Debugging");
		this->addChild(debugText);
	}

	void step() override
	{
		auto module = dynamic_cast<FV1EmuModule *>(this->module);
		if (module)
		{
			debugText->visible = module->Debug;
			if (module->Debug)
				debugText->setText(module->fx.dumpState("\n"));
		}
		ModuleWidget::step();
	}

};

Model *modelFV1Emu = createModel<FV1EmuModule, FV1EmuWidget>("FV-1emu");
