/* FV1 VCV PlugIn
 * Copyright (C)2018-2020 - Eduard Heidt
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

#include "plugin.hpp"
#include <osdialog.h>
#include <iterator>
#include <thread>
#include <fstream>
#include "FV1emu.hpp"

struct FV1EmuModule : Module
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
        TPOT_DRYWWET,
        TPOT_LIN,
        TPOT_RIN,
        NUM_PARAMS
    };
    enum InputIds
    {
        POT_0,
        POT_1,
        POT_2,
        INPUT_L,
        INPUT_R,
        CV_DRYWET,
        NUM_INPUTS
    };
    enum OutputIds
    {
        OUTPUT_L,
        OUTPUT_R,
        NUM_OUTPUTS
    };

    FV1emu fx;

    std::string programs_json = asset::plugin(pluginInstance, "fx/programs.json");
    bool Debug = false;
    int selectedProgram = -1;
    std::vector<json_t *> programs;
    std::map<std::string, std::vector<size_t>> categories;
    std::set<int> favourites = {}; //{39, 143, 118, 135, 131, 110};

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
        configParam(TPOT_DRYWWET, -1.0f, 1.0f, 0.0f, "");
        configParam(DRYWET_PARAM, -1.0f, 1.0f, 0.0f, "");
        configParam(TPOT_LIN, -1.0f, 1.0f, 0.0f, "");
        configParam(TPOT_RIN, -1.0f, 1.0f, 0.0f, "");

        loadFx(asset::plugin(pluginInstance, "fx/demo.spn"));
        INFO("FV1EmuModule()");
    }

    ~FV1EmuModule()
    {
        INFO("~FV1EmuModule()");
    }

    std::vector<std::string> getCategories(int i)
    {
        std::vector<std::string> categories;

        auto entryJ = this->programs[i];

        if (false)
        {
            json_t *authorJ = json_object_get(entryJ, "application");
            std::string category = authorJ != nullptr ? json_string_value(authorJ) : "unknown";
            categories.push_back(category);
        }
        else
        {
            json_t *categoriesJ = json_object_get(entryJ, "categories");
            size_t ci;
            json_t *centryJ;

            json_array_foreach(categoriesJ, ci, centryJ)
            {
                std::string category = json_string_value(centryJ);
                categories.push_back(category);
            }
        }

        return categories;
    }

    bool loadPrograms(const std::string &programs_json)
    {
        this->programs_json = programs_json;
        categories.clear();
        programs.clear();

        if (system::isFile(programs_json))
        {
            INFO("Loading json %s", programs_json.c_str());
            FILE *file = std::fopen(programs_json.c_str(), "r");
            if (!file)
            {
                // Exit silently
                return false;
            }
            DEFER({
                std::fclose(file);
            });

            json_error_t error;
            json_t *rootJ = json_loadf(file, 0, &error);
            if (!rootJ)
            {
                std::string message = string::f("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
                return false;
            }
            DEFER({
                json_decref(rootJ);
            });

            size_t i;
            json_t *entryJ;
            json_array_foreach(rootJ, i, entryJ)
            {
                programs.push_back(entryJ);

                for (auto category : getCategories(i))
                {
                    json_incref(entryJ);
                    categories[category].push_back(programs.size() - 1);
                }
            }
        }

        return true;
    }

    void process(const ProcessArgs &args) override
    {
        if (this->selectedProgram >= 0)
        {
            if (nextTrigger.process(params[FX_NEXT].getValue()))
            {
                this->selectedProgram++;
                this->selectedProgram = this->selectedProgram % this->programs.size();

                loadProgram(this->selectedProgram);
            }

            if (prevTrigger.process(params[FX_PREV].getValue()))
            {
                if (--this->selectedProgram <= 0)
                    this->selectedProgram = this->programs.size() - 1;

                loadProgram(this->selectedProgram);
            }
        }
        else if (filesInPath.size() > 0)
        {
            if (nextTrigger.process(params[FX_NEXT].getValue()))
            {
                auto it = std::find(filesInPath.cbegin(), filesInPath.cend(), lastPath);
                ;
                if (it == filesInPath.cend() || ++it == filesInPath.cend())
                    it = filesInPath.cbegin();

                loadFx(*it, false);
            }

            if (prevTrigger.process(params[FX_PREV].getValue()))
            {
                auto it = std::find(filesInPath.crbegin(), filesInPath.crend(), lastPath);

                if (it == filesInPath.crend() || ++it == filesInPath.crend())
                    it = filesInPath.crbegin();

                loadFx(*it, false);
            }
        }

        //float deltaTime = args.sampleTime;
        auto inL = inputs[INPUT_L].getVoltage() * (1 + params[TPOT_LIN].getValue());
        auto inR = inputs[INPUT_R].getVoltage() * (1 + params[TPOT_RIN].getValue());

        // if(inputs[INPUT_L].isConnected() == false)
        //     inL = inR;

        // if(inputs[INPUT_R].isConnected() == false)
        //     inR = inL;

        auto outL = 0.0f;
        auto outR = 0.0f;

        auto p0 = params[POT0_PARAM].getValue();
        auto p1 = params[POT1_PARAM].getValue();
        auto p2 = params[POT2_PARAM].getValue();

        p0 += inputs[POT_0].getVoltage() * 0.1f * params[TPOT0_PARAM].getValue();
        p1 += inputs[POT_1].getVoltage() * 0.1f * params[TPOT1_PARAM].getValue();
        p2 += inputs[POT_2].getVoltage() * 0.1f * params[TPOT2_PARAM].getValue();

        float mix = params[DRYWET_PARAM].getValue();
        mix += inputs[CV_DRYWET].getVoltage() * params[TPOT_DRYWWET].getValue();
        float d = clamp(1.f - mix, 0.0f, 1.0f);
        float w = clamp(1.f + mix, 0.0f, 1.0f);

        if (w > 0)
        {
            fx.run(inL * 0.1, inR * 0.1, p0, p1, p2, outL, outR);
            outL *= 10;
            outR *= 10;
        }

        outputs[OUTPUT_L].setVoltage(clamp(inL * d + outL * w, -10.0f, 10.0f));
        outputs[OUTPUT_R].setVoltage(clamp(inR * d + outR * w, -10.0f, 10.0f));
    }

    json_t *dataToJson() override
    {
        json_t *rootJ = json_object();

        if (this->selectedProgram >= 0)
        {
            auto base64 = getBase64SPN(this->selectedProgram);
            json_object_set_new(rootJ, "base64", json_string(base64.c_str()));
            json_object_set_new(rootJ, "display", json_string(this->display.c_str()));
        }
        else
            json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));

        json_object_set_new(rootJ, "programsJson", json_string(programs_json.c_str()));

        return rootJ;
    }

    void dataFromJson(json_t *rootJ) override
    {
        if (json_t *j = json_object_get(rootJ, "programsJson"))
        {
            this->programs_json = json_string_value(j);
        }

        loadPrograms(programs_json);

        if (json_t *j = json_object_get(rootJ, "base64"))
        {
            auto base64 = json_string_value(j);
            for (int i = 0; i < (int)this->programs.size(); i++)
            {
                if (base64 == getBase64SPN(i))
                    loadProgram(i);
            }

            if (this->selectedProgram < 0)
            {
                auto program = string::fromBase64(base64);
                program.push_back(0);

                if (this->fx.loadFromSPN("", (const char *)&program[0]))
                {
                    this->display = std::to_string(0) + ": " + this->fx.getDisplay();
                }
                else
                    this->display = std::to_string(0) + ": !!! " + this->fx.getDisplay();

                if (json_t *jd = json_object_get(rootJ, "display"))
                {
                    this->display = json_string_value(jd);
                }
            }

            return;
        }

        if (json_t *j = json_object_get(rootJ, "lastPath"))
        {
            auto file = json_string_value(j);
            loadFx(file);
        }
    }

    std::string display;
    std::string lastPath;
    std::vector<std::string> filesInPath;

    std::string getBase64SPN(int i)
    {
        auto entryJ = this->programs[i];
        auto downloadJ = json_object_get(entryJ, "download");
        auto spnJ = json_object_get(downloadJ, "spn");
        auto base64J = json_object_get(spnJ, "base64");
        return json_string_value(base64J);
    }

    void loadProgram(int i)
    {
        auto base64 = getBase64SPN(i);
        auto program = string::fromBase64(base64);
        program.push_back(0);

        auto entryJ = this->programs[i];
        json_t *nameJ = json_object_get(entryJ, "name");
        std::string name = json_string_value(nameJ);

        if (this->fx.loadFromSPN(name, (const char *)&program[0]))
        {
            this->display = std::to_string(i) + ": " + this->fx.getDisplay();
        }
        else
            this->display = std::to_string(i) + ": !!! " + this->fx.getDisplay();

        if (json_t *controlsJ = json_object_get(entryJ, "controls"))
        {
            this->display = std::to_string(i) + ": " + name + "\n";
            size_t i;
            json_t *entryJ;
            json_array_foreach(controlsJ, i, entryJ)
            {
                auto ctrl = json_string_value(entryJ);
                this->display += std::string("P") + std::to_string(i) + ": " + ctrl + "\n";
            }
        }

        this->selectedProgram = i;
    }

    void loadFx(const std::string &file, bool scanDir = true)
    {
        this->selectedProgram = -1;
        this->lastPath = file;
        this->fx.load(file);

        if (scanDir)
        {
            filesInPath.clear();
            auto dir = system::getDirectory(this->lastPath);
            for (auto name : system::getEntries(dir, 10))
            {
                std::size_t found = name.find(".spn", name.length() - 5);
                if (found == std::string::npos)
                    found = name.find(".spn", name.length() - 5);

                if (found != std::string::npos)
                {
                    name = system::getAbsolute(name);
                    filesInPath.push_back(name);
                }
            }

            std::sort(filesInPath.begin(), filesInPath.end());
        }

        auto it = std::find(filesInPath.cbegin(), filesInPath.cend(), lastPath);
        auto fxIndex = it - filesInPath.cbegin();

        this->display = std::to_string(fxIndex) + ": " + this->fx.getDisplay();
        
    }
};

struct ProgramMenuItem : MenuItem
{
    int program_index;
    FV1EmuModule *module;
    void onAction(const event::Action &e) override
    {
        module->loadProgram(program_index);
    }

    void step() override
    {
        auto entryJ = module->programs[program_index];

        auto nameJ = json_object_get(entryJ, "name");
        std::string name = json_string_value(nameJ);
        auto authorJ = json_object_get(entryJ, "author");
        std::string author = authorJ != nullptr ? json_string_value(authorJ) : "";

        if (module->favourites.find(program_index) != module->favourites.end())
            text = std::string("★") + " " + std::to_string(program_index) + ": " + name;
        else
            text = std::to_string(program_index) + ": " + name;

        rightText = author; //CHECKMARK(false);
        //rightText = CHECKMARK(module->selectedProgram == program_index);
        MenuItem::step();
    }
};

struct ProgramsMenuItem : MenuItem
{
    FV1EmuModule *module;

    Menu *createChildMenu() override
    {
        Menu *menu = new Menu;

        if (module->categories.find(this->text) != module->categories.end())
        {
            for (auto i : module->categories[this->text])
            {
                if (auto *item = new ProgramMenuItem())
                {
                    item->module = module;
                    menu->addChild(item);
                    item->program_index = i;
                }
            }
        }

        return menu;
    }
};

struct OpenSpnMenuItem : MenuItem
{
    FV1EmuModule *module;
    void onAction(const event::Action &e) override
    {
        auto dir = module->lastPath.empty() ? asset::user("") : system::getDirectory(module->lastPath);
        auto *filters = osdialog_filters_parse("FV1-FX Asm:spn");
        char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
        if (path)
        {
            module->loadFx(path);
            free(path);
        }
    }
    void step() override
    {
        this->text = "Select SPN File...";
        MenuItem::step();
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
        rightText = (module->Debug == true) ? "✔" : "";
        MenuItem::step();
    }
};

struct logParserMenuItem : MenuItem
{
    FV1EmuModule *module;
    void onAction(const event::Action &e) override
    {
        auto spn_parser_log = system::getAbsolute(asset::plugin(pluginInstance, "spn_parser.log"));

        std::ofstream out(spn_parser_log.c_str());
        out << module->fx.log.str();
        out.close();
        INFO("%s", spn_parser_log.c_str());
        system::openBrowser(spn_parser_log);
    }
    void step() override
    {
        MenuItem::step();
    }
};

struct SelectBankMenuItem : MenuItem
{
    FV1EmuModule *module;
    void onAction(const event::Action &e) override
    {
        if (true)
        {
            auto dir = module->lastPath.empty() ? asset::user("") : system::getDirectory(module->lastPath);
            auto *filters = osdialog_filters_parse("FV1-Programs:json");
            char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
            if (path)
            {
                module->loadPrograms(path);
                free(path);

                if (module->programs.size() > 0)
                {
                    std::string message = string::f("FV1-programs have been loaded.\nReopen the context menu and select one of the %d FV1-program.", (int)module->programs.size());
                    osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, message.c_str());
                }
                else
                {
                    std::string message = string::f("Invalid JSON file loaded.");
                    osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, message.c_str());
                }
            }
        }
        else
        {
            //experimental Code -> Downloading FX-Banks directly from github... (nut used yet!!)

            auto programs_json = module->programs_json;
            //if (!system::isFile(programs_json))
            {
                float progress;
                network::requestDownload("https://raw.githubusercontent.com/eh2k/fv1-emu/gh-pages/programs.json", programs_json, &progress);
            }

            module->loadPrograms(programs_json);

            if (module->programs.size() > 0)
            {
                std::string message = string::f("FV1-programs have been downloaded.\nReopen the context menu and select one of the %d FV1-program.", (int)module->programs.size());
                osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, message.c_str());
            }
            else
            {
                system::openBrowser("https://github.com/eh2k/fv1-emu#effects--dsp-programs");
            }
        }
    }
    void step() override
    {
        this->text = "Select FX-Bank...";
        //this->rightText = "★"; //CHECKMARK(false);
        MenuItem::step();
    }
};

struct DisplayPanel : TransparentWidget //LedDisplayChoice
{
    const std::string &text;
    FV1EmuModule *module;

    DisplayPanel(const Vec &pos, const Vec &size, FV1EmuModule *module) : text(module->display)
    {
        this->module = module;
        box.pos = pos;
        box.size = size;
    }

    void onButton(const event::Button &e) override
    {
        TransparentWidget::onButton(e);

        if (e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT))
        {
            event::Action eAction;
            onAction(eAction);
            e.consume(this);
        }
    }

    void onAction(const event::Action &e) override
    {

        ui::Menu *menu = createMenu();

        if (auto item = new OpenSpnMenuItem)
        {
            item->module = module;
            menu->addChild(item);
        }

        if (auto item = new SelectBankMenuItem)
        {
            item->module = module;
            menu->addChild(item);
        }

        if (module->programs.size() == 0)
        {
        }
        else
        {
            menu->addChild(new MenuSeparator());

            if (module->favourites.size() > 0)
            {
                for (auto i : module->favourites)
                {
                    if (auto *item = new ProgramMenuItem())
                    {
                        item->module = module;
                        menu->addChild(item);
                        item->program_index = i;
                    }
                }

                menu->addChild(new MenuSeparator());
            }

            for (auto e : module->categories)
            {
                if (auto item = new ProgramsMenuItem)
                {
                    item->text = e.first;
                    item->rightText = RIGHT_ARROW; //CHECKMARK(false);
                    item->module = module;
                    menu->addChild(item);
                }
            }
        }

        menu->addChild(new MenuSeparator());

        DebugMenuItem *debugItem = new DebugMenuItem;
        debugItem->text = "DEBUG";
        debugItem->module = module;
        menu->addChild(debugItem);

        logParserMenuItem *plogItem = new logParserMenuItem;
        plogItem->text = "Parser log...";
        plogItem->module = module;
        menu->addChild(plogItem);
    }

    void draw(NVGcontext *vg) override
    {
        std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        nvgFontSize(vg, 11);
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, 1));
        if (font) {
            nvgFontFaceId(vg, font->handle);
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
    }
};

struct DebugPanel : LedDisplay {
    LedDisplayTextField* textField = createWidget<LedDisplayTextField>(Vec(0, 0));
    FV1EmuModule *module;
    DebugPanel() {
	    textField->multiline = true;
        textField->box.size = box.size;
		addChild(textField);
    }
    void setText(std::string text) {
        textField->text = text;
    }
    void onButton(const event::Button &e) override
    {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0)
            module->Debug = false;
        LedDisplay::onButton(e);
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

        auto *pathItem = new OpenSpnMenuItem;
        pathItem->module = module;
        menu->addChild(pathItem);

        DebugMenuItem *debugItem = new DebugMenuItem;
        debugItem->text = "DEBUG";
        debugItem->module = module;
        menu->addChild(debugItem);

        logParserMenuItem *plogItem = new logParserMenuItem;
        plogItem->text = "Parser log...";
        plogItem->module = module;
        menu->addChild(plogItem);
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
            auto display = new DisplayPanel(Vec(12, 10.5 * RACK_GRID_WIDTH), Vec(200, 75), module);
            addChild(display);
            addParam(createParam<TL1105>(display->box.pos.plus(Vec(93, 53)), module, FV1EmuModule::FX_PREV));
            addParam(createParam<TL1105>(display->box.pos.plus(Vec(118, 53)), module, FV1EmuModule::FX_NEXT));
        }

        auto d = (box.size.x - (RACK_GRID_WIDTH * 3)) / 3;
        auto center = PJ301MPort().box.size.mult(-0.5);

        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 3)), module, FV1EmuModule::POT_0));
        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 1, RACK_GRID_WIDTH * 3)), module, FV1EmuModule::POT_1));
        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 2, RACK_GRID_WIDTH * 3)), module, FV1EmuModule::POT_2));
        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 3)), module, FV1EmuModule::CV_DRYWET));

        center = Trimpot().box.size.mult(-0.5);

        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 5.25)), module, FV1EmuModule::TPOT0_PARAM));
        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 1, RACK_GRID_WIDTH * 5.25)), module, FV1EmuModule::TPOT1_PARAM));
        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 2, RACK_GRID_WIDTH * 5.25)), module, FV1EmuModule::TPOT2_PARAM));
        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 5.25)), module, FV1EmuModule::TPOT_DRYWWET));

        center = RoundBlackKnob().box.size.mult(-0.5);

        addParam(createParam<RoundBlackKnob>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 7.5)), module, FV1EmuModule::POT0_PARAM));
        addParam(createParam<RoundBlackKnob>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 1, RACK_GRID_WIDTH * 7.5)), module, FV1EmuModule::POT1_PARAM));
        addParam(createParam<RoundBlackKnob>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 2, RACK_GRID_WIDTH * 7.5)), module, FV1EmuModule::POT2_PARAM));       
        addParam(createParam<RoundBlackKnob>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 7.5)), module, FV1EmuModule::DRYWET_PARAM));


        center = Trimpot().box.size.mult(-0.5);

        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 17.0)), module, FV1EmuModule::TPOT_LIN));
        addParam(createParam<Trimpot>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 17.0)), module, FV1EmuModule::TPOT_RIN));

        center = PJ301MPort().box.size.mult(-0.5);

        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 19.5)), module, FV1EmuModule::INPUT_L));
        addInput(createInput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 19.5)), module, FV1EmuModule::INPUT_R));

        addOutput(createOutput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 0, RACK_GRID_WIDTH * 22.5)), module, FV1EmuModule::OUTPUT_L));
        addOutput(createOutput<PJ301MPort>(center.plus(Vec(RACK_GRID_WIDTH * 1.5 + d * 3, RACK_GRID_WIDTH * 22.5)), module, FV1EmuModule::OUTPUT_R));

        debugText = new DebugPanel();
        debugText->module = module;
        debugText->box.size = box.size;
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
