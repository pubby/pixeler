#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/bookctrl.h>
#include <wx/mstream.h>
#include <wx/clipbrd.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/clrpicker.h>

#include <filesystem>
#include <cstring>
#include <map>

#include "model.hpp"
#include "convert.hpp"
#include "id.hpp"

enum
{
    ID_AUTO_COLOR,
};

class app_t: public wxApp
{
    bool OnInit();
    
    // In your App class that derived from wxApp
    virtual bool OnExceptionInMainLoop() override
    {
        try { throw; }
        catch(std::exception &e)
        {
            wxMessageBox(e.what(), "C++ Exception Caught", wxOK);
        }
        return true;   // continue on. Return false to abort program
    }

public:
        
};

IMPLEMENT_APP(app_t)

class visual_t : public wxScrolledWindow
{
public:
    explicit visual_t(wxWindow* parent, model_t& model, bool picker = false)
    : wxScrolledWindow(parent)
    , model(model)
    , picker(picker)
    {
        resize();

        SetDoubleBuffered(true);
        if(picker)
            Bind(wxEVT_LEFT_DOWN, &visual_t::on_click, this);
        else
            Bind(wxEVT_MOUSEWHEEL, &visual_t::on_wheel, this);

        Bind(wxEVT_UPDATE_UI, &visual_t::on_update, this);
        Connect(wxEVT_PAINT, wxPaintEventHandler(visual_t::on_paint), 0, this);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
    }

    void on_update(wxUpdateUIEvent&) { resize(); }

    void on_paint(wxPaintEvent& event)
    {
#if GC_RENDER
        wxPaintDC dc(this);
        dc.Clear();

#ifdef __WXMSW__
        wxGraphicsRenderer* renderer = wxGraphicsRenderer::GetDirect2DRenderer();
#else
        wxGraphicsRenderer* renderer = wxGraphicsRenderer::GetCairoRenderer();
#endif

        std::unique_ptr<wxGraphicsContext> gc(renderer->CreateContext(dc));
        if(gc)
        {
            gc->SetInterpolationQuality(wxINTERPOLATION_NONE);
            gc->SetAntialiasMode(wxANTIALIAS_NONE);
            gc->Translate(-GetViewStart().x, -GetViewStart().y);
            gc->Scale(scale, scale);
            on_draw(*gc);
        }
#else
        wxPaintDC dc(this);
        PrepareDC(dc);
        dc.SetUserScale(scale, scale);
        on_draw(dc);
#endif
    }

    void on_draw(render_t& gc)
    {
        if(picker)
        {
            if(model.picker_bitmap.IsOk())
            {
#ifdef GC_RENDER
                gc.DrawBitmap(model.picker_bitmap, 0, 0, 512, 512);
#else
                gc.DrawBitmap(model.picker_bitmap, 0, 0);
#endif
            }
        }
        else
        {
            auto& bitmap = model.display ? model.output_bitmap : model.base_bitmap;
            if(bitmap.IsOk())
            {
#ifdef GC_RENDER
                gc.DrawBitmap(bitmap, 0, 0, model.w, model.h);
#else
                gc.DrawBitmap(bitmap, 0, 0);
#endif
            }
        }
    }

    void resize(bool force = false)
    {
        if(picker)
        {
            SetMinSize({ 512, 512 });
            SetMaxSize({ 512, 512 });
            SetVirtualSize(512, 512);
            return;
        }

        if(w != model.w || h != model.h)
        {
            w = model.w;
            h = model.h;
            scale = 512 / std::max(w, h);
            force = true;
        }


        if(force)
        {
            w = model.w;
            h = model.h;
            SetMinSize({ 512 + 16, 512 + 16 });
            SetMaxSize({ 1024 + 16, 1024 + 16 });
            SetVirtualSize(w * scale, h * scale);
            SetScrollRate(1, 1);
        }
    }

    void set_zoom(int new_scale)
    {
        if(picker)
            return;

        if(new_scale == scale)
            return;

        scale = new_scale;

        resize(true);
        Refresh();
    }

    void on_wheel(wxMouseEvent& event)
    {
        if(picker)
            return;

        int const rot = event.GetWheelRotation();
        int const delta = event.GetWheelDelta();
        int const turns = rot / delta;
        int const target = turns >= 0 ? (scale << turns) : (scale >> -turns);
        int const diff = target - scale;

        set_zoom(std::clamp(scale + diff, 1, 16));
    }

    void on_click(wxMouseEvent& event)
    {
        if(model.picker_image.IsOk() && picker_fn)
        {
            SetFocus();
            int x = std::clamp(event.GetPosition().x, 0, model.picker_image.GetWidth());
            int y = std::clamp(event.GetPosition().y, 0, model.picker_image.GetHeight());
            rgb_t read;
            read.r = model.picker_image.GetRed(x, y);
            read.g = model.picker_image.GetGreen(x, y);
            read.b = model.picker_image.GetBlue(x, y);
            picker_fn(read);
        }
    }

    std::function<void(rgb_t)> picker_fn;

protected:
    model_t& model;
    bool picker;
    int scale = 1;
    int w = 0;
    int h = 0;
};

class color_button_t : public wxButton
{
public:
    color_button_t(wxWindow* parent, model_t const& model, std::uint8_t new_color, color_knob_t* knob=nullptr)
    : wxButton(parent, wxID_ANY)
    , model(model)
    , knob(knob)
    {
        SetMaxSize(wxSize(64, -1));
        set_color(new_color);
    }

    void manual_update() { if(knob) set_color(knob->nes_color); }

    std::uint8_t get_color() const { return color; }
    void set_color(std::uint8_t new_color)
    {
        color = new_color;
        SetLabel(color_string(color));
        SetBitmap(model.color_bitmaps[std::min<std::uint8_t>(color, 64)]);
    }

    color_knob_t* knob;
private:
    std::uint8_t color;
    model_t const& model;
};

class rgb_button_t : public wxButton
{
public:
    rgb_button_t(wxWindow* parent, model_t const& model, color_knob_t* knob, unsigned index)
    : wxButton(parent, wxID_ANY)
    , model(model)
    , knob(knob)
    , index(index)
    {
        SetMaxSize(wxSize(32, -1));
        update_color();
    }

    void manual_update() { update_color(); }

    void update_color()
    {
        if(knob->map_enable[index])
        {
            rgb_t rgb = knob->map_colors[index];
            SetBackgroundColour(wxColour(rgb.r, rgb.g, rgb.b));
        }
        else
        {
            SetBackgroundColour(wxColour(0, 0, 0, 0));
            ClearBackground();
        }
    }

    color_knob_t* knob;
    unsigned index;
private:
    std::uint8_t color;
    model_t const& model;
};

class color_dialog_t : public wxDialog
{
public:
    color_dialog_t(wxWindow* parent, model_t const& model)
    : wxDialog(parent, wxID_ANY, "NES Color Picker")
    , model(model)
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        for(unsigned y = 0; y < 14; y += 1)
        {
            wxPanel* panel = new wxPanel(this);
            wxBoxSizer* panel_sizer = new wxBoxSizer(wxHORIZONTAL);

            for(unsigned x = 0; x < 4; x += 1)
            {
                unsigned color = x * 16 + y;

                if(y == 0)
                {
                    if(x == 0)
                        color = 0x0F;
                    else
                        color -= 16;
                }

                if(y == 13)
                {
                    color = 0xFF;
                    if(x != 0)
                        break;
                }

                color_button_t* btn = new color_button_t(panel, model, color);
                btn->Bind(wxEVT_BUTTON, &color_dialog_t::on_click, this);
                panel_sizer->Add(btn, wxSizerFlags());
            }
            
            panel->SetSizerAndFit(panel_sizer);
            sizer->Add(panel, wxSizerFlags());
        }

        SetSizerAndFit(sizer);

    }

    void on_click(wxCommandEvent& event)
    {
        color = static_cast<color_button_t*>(event.GetEventObject())->get_color();
        EndModal(wxID_OK);
    }

    std::uint8_t color = 0xFF;
private:
    model_t const& model;
};

class rgb_dialog_t : public wxDialog
{
public:
    rgb_dialog_t(wxWindow* parent, model_t& model, std::uint8_t nes_color, rgb_t const& initial_rgb)
    : wxDialog(parent, wxID_ANY, "RGB Color Picker")
    , model(model)
    , nes_color(nes_color)
    , rgb(initial_rgb)
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        visual_t* visual = new visual_t(this, model, true);
        visual->picker_fn = [&](rgb_t new_rgb)
        {
            rgb = new_rgb;
            color_ctrl->SetColour(wxColour(new_rgb.r, new_rgb.g, new_rgb.b));
        };
        sizer->Add(visual, wxSizerFlags());

        color_ctrl = new wxColourPickerCtrl(this, wxID_ANY);
        color_ctrl->SetColour(wxColour(rgb.r, rgb.g, rgb.b));
        color_ctrl->Bind(wxEVT_COLOURPICKER_CHANGED, &rgb_dialog_t::on_color_pick, this);
        sizer->Add(color_ctrl, wxSizerFlags().Expand());

        wxPanel* panel = new wxPanel(this);
        panel->SetMinSize(wxSize(512, -1));
        wxBoxSizer* panel_sizer = new wxBoxSizer(wxHORIZONTAL);

        wxButton* no_color  = new wxButton(panel, wxID_ANY, "Remove Mapping");
        no_color->Bind(wxEVT_BUTTON, &rgb_dialog_t::on_remove, this);
        panel_sizer->Add(no_color, wxSizerFlags(wxALL | wxALIGN_CENTER));

        wxButton* nes = new wxButton(panel, wxID_ANY, "Use NES Color");
        nes->Bind(wxEVT_BUTTON, &rgb_dialog_t::on_nes_color, this);
        panel_sizer->Add(nes, wxSizerFlags(wxALL | wxALIGN_CENTER));

        wxButton* ok = new wxButton(panel, wxID_OK, "Use Selected");
        panel_sizer->Add(ok, wxSizerFlags(wxALL | wxALIGN_CENTER));

        sizer->Add(panel, wxALIGN_CENTER_HORIZONTAL | wxALL | wxEXPAND);
        panel->SetSizer(panel_sizer);

        SetSizerAndFit(sizer);

        Bind(wxEVT_CLOSE_WINDOW, &rgb_dialog_t::on_close, this);
    }

    void on_close(wxCloseEvent& event)
    {
        EndModal(wxID_CANCEL);
    }

    void on_color_pick(wxColourPickerEvent& event)
    {
        auto color = event.GetColour();
        rgb = { color.GetRed(), color.GetGreen(), color.GetBlue() };
    }

    void on_remove(wxCommandEvent& event)
    {
        EndModal(wxID_DELETE);
    }

    void on_nes_color(wxCommandEvent& event)
    {
        if(nes_color < 64)
        {
            rgb = nes_colors[nes_color];
            color_ctrl->SetColour(wxColour(rgb.r, rgb.g, rgb.b));
            EndModal(wxID_OK);
        }
        else
            EndModal(wxID_DELETE);
    }

    std::uint8_t const nes_color;
    rgb_t rgb;
private:
    model_t& model;
    visual_t* visual;
    wxColourPickerCtrl* color_ctrl;
};

wxWindow* get_top(wxWindow* pWindow)
{
    wxWindow* pWin = pWindow;
    while(true)
    {
        if(pWin->GetParent())
            pWin = pWin->GetParent();
        else
            break;
    }
    return pWin;
}

class pal_entry_t : public wxPanel
{
public:
    pal_entry_t(wxWindow* parent, model_t& model, color_knob_t& knob)
    : wxPanel(parent)
    , model(model)
    , knob(knob)
    {
        color = new color_button_t(this, model, knob.nes_color, &knob);
        color->Bind(wxEVT_BUTTON, &pal_entry_t::on_click_nes, this);

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(color, wxSizerFlags());
        for(unsigned i = 0; i < 4; i += 1)
        {
            map_colors[i] = new rgb_button_t(this, model, &knob, i);
            map_colors[i]->Bind(wxEVT_BUTTON, &pal_entry_t::on_click_rgb, this);
            sizer->Add(map_colors[i]);
        }

        greed = new wxSlider(this, wxID_ANY, 0, -20, 20, wxDefaultPosition, wxSize(180, -1), wxSL_HORIZONTAL | wxSL_AUTOTICKS);
        greed->Bind(wxEVT_SCROLL_CHANGED, &pal_entry_t::on_greed, this);

        bleed = new wxSlider(this, wxID_ANY, 0, -20, 20, wxDefaultPosition, wxSize(100, -1), wxSL_HORIZONTAL | wxSL_AUTOTICKS);
        bleed->Bind(wxEVT_SCROLL_CHANGED, &pal_entry_t::on_bleed, this);

        sizer->Add(greed, wxSizerFlags());
        sizer->Add(bleed, wxSizerFlags());

        SetSizerAndFit(sizer);
    }

    void manual_update() 
    { 
        greed->SetValue(knob.greed);
        bleed->SetValue(knob.bleed);
        for(auto* ptr : map_colors)
            ptr->manual_update();
        color->manual_update();
    }

    void on_click_nes(wxCommandEvent& event)
    {
        color_dialog_t dlg(this, model);
        
        if(dlg.ShowModal() == wxID_OK) 
        {
            color_button_t* btn = static_cast<color_button_t*>(event.GetEventObject());
            btn->set_color(dlg.color);

            if(dlg.color < 64 
               && (!btn->knob->any_enabled() 
                   || (btn->knob->map_enable[0] 
                       && btn->knob->map_colors[0] == nes_colors[btn->knob->nes_color])))
            {
                btn->knob->map_enable[0] = true;
                btn->knob->map_colors[0] = nes_colors[dlg.color];
                map_colors[0]->update_color();
            }

            btn->knob->nes_color = dlg.color;

            model.update();

            auto* top = get_top(this);
            top->Layout();
            top->Update();
            top->Refresh();
        }
    }

    void on_click_rgb(wxCommandEvent& event)
    {
        auto btn = static_cast<rgb_button_t*>(event.GetEventObject());
        unsigned index = btn->index;
        rgb_dialog_t dlg(this, model, knob.nes_color, knob.map_colors[index]);
        
        auto result = dlg.ShowModal();
        if(result == wxID_OK) 
        {
            knob.map_enable[index] = true;
            knob.map_colors[index] = dlg.rgb;
            btn->update_color();
            model.update();
            auto* top = get_top(this);
            top->Layout();
            top->Update();
            top->Refresh();
        }
        else if(result == wxID_DELETE) 
        {
            knob.map_enable[index] = false;
            btn->update_color();
            model.update();
            auto* top = get_top(this);
            top->Layout();
            top->Update();
            top->Refresh();
        }
    }

    void on_greed(wxScrollEvent& event)
    {
        if(knob.set_greed(event.GetPosition()))
        {
            model.update();

            auto* top = get_top(this);
            top->Layout();
            top->Update();
            top->Refresh();
        }
    }

    void on_bleed(wxScrollEvent& event)
    {
        if(knob.set_bleed(event.GetPosition()))
        {
            model.update();

            auto* top = get_top(this);
            top->Layout();
            top->Update();
            top->Refresh();
        }
    }

private:
    model_t& model;
    color_knob_t& knob;
    color_button_t* color;
    std::array<rgb_button_t*, 4> map_colors;
    wxSlider* greed;
    wxSlider* bleed;
};

class auto_color_dialog_t : public wxDialog
{
public:
    auto_color_dialog_t(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Automatic Colors")
    {
        SetMinSize(wxSize(128, 64));

        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        count = new wxSpinCtrl(this);
        count->SetRange(1, 16);
        count->SetValue(4);

        map = new wxCheckBox(this, wxID_ANY, "Create RGB Mapping");

        sizer->Add(count, 0, wxALL | wxALIGN_CENTER, 4);
        sizer->Add(map, 0, wxALL | wxALIGN_CENTER, 4);

        wxSizer* bs = CreateButtonSizer(wxOK | wxCANCEL);
        sizer->Add(bs, 0, wxALL | wxALIGN_CENTER, 8);

        SetSizerAndFit(sizer);
    }

    wxSpinCtrl* count;
    wxCheckBox* map;
};

class frame_t : public wxFrame
{
public:
    frame_t()
    : wxFrame(nullptr, wxID_ANY, "Pixeler", wxDefaultPosition, wxSize(1024, 768))
    {
        SetMinSize(wxSize(1024, 768));
        wxMenu* menu_file = new wxMenu;
        menu_file->Append(wxID_OPEN, "&Open Image\tCTRL+O");
        menu_file->Append(wxID_SAVE, "&Save Image\tCTRL+S");
        menu_file->Append(wxID_SAVEAS, "Save Image &As\tSHIFT+CTRL+S");
        menu_file->AppendSeparator();
        menu_file->Append(wxID_EXIT);

        wxMenu* menu_edit = new wxMenu;
        copy = menu_edit->Append(wxID_COPY, "Copy Image\tCTRL+C");
        paste = menu_edit->Append(wxID_PASTE, "Paste Image (Preview must be off)\tCTRL+V");
        menu_edit->AppendSeparator();
        menu_edit->Append(wxID_NEW, "Reset Colors\tCTRL+N");
        menu_edit->Append(ID_AUTO_COLOR, "Automatic Colors");

        wxMenuBar* menu_bar = new wxMenuBar;
        menu_bar->Append(menu_file, "&File");
        menu_bar->Append(menu_edit, "&Edit");
        

        SetMenuBar(menu_bar);
     
        model.status_bar = CreateStatusBar();

        wxPanel* l_panel = new wxPanel(this);
        wxPanel* r_panel = new wxPanel(this);

        visual = new visual_t(l_panel, model);
        wxPanel* wh_panel = new wxPanel(l_panel);

        {
            wxStaticText* size_label = new wxStaticText(wh_panel, wxID_ANY, "Converted size:");
            wxStaticText* preview_label = new wxStaticText(wh_panel, wxID_ANY, "  Preview:");

            w_ctrl= new wxSpinCtrl(wh_panel);
            w_ctrl->SetRange(8, 512);
            w_ctrl->SetIncrement(8);
            w_ctrl->SetValue(model.w);

            h_ctrl= new wxSpinCtrl(wh_panel);
            h_ctrl->SetRange(8, 512);
            h_ctrl->SetIncrement(8);
            h_ctrl->SetValue(model.h);

            display_checkbox = new wxCheckBox(wh_panel, wxID_ANY, "");
            display_checkbox->SetValue(model.display);

            wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(size_label, wxSizerFlags().Border(wxALL));
            sizer->Add(w_ctrl, wxSizerFlags().Border(wxRIGHT));
            sizer->Add(h_ctrl, wxSizerFlags());
            sizer->Add(preview_label, wxSizerFlags().Border(wxALL));
            sizer->Add(display_checkbox, wxSizerFlags().Border(wxALL));

            w_ctrl->Bind(wxEVT_SPINCTRL, &frame_t::on_change_w<wxSpinEvent>, this);
            h_ctrl->Bind(wxEVT_SPINCTRL, &frame_t::on_change_h<wxSpinEvent>, this);
            w_ctrl->Bind(wxEVT_TEXT, &frame_t::on_change_w<wxCommandEvent>, this);
            h_ctrl->Bind(wxEVT_TEXT, &frame_t::on_change_h<wxCommandEvent>, this);
            display_checkbox->Bind(wxEVT_CHECKBOX, &frame_t::on_change_display, this);

            wh_panel->SetSizerAndFit(sizer);
        }

        {
            wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
            for(unsigned i = 0; i < model.color_knobs.size(); i += 1)
            {
                pal_entry_t* entry = new pal_entry_t(r_panel, model, model.color_knobs[i]);
                sizer->Add(entry, wxSizerFlags());
                pal_entries.push_back(entry);
            }
            r_panel->SetSizer(sizer);
        }

        wxPanel* dither_panel = new wxPanel(l_panel);
        {
            wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

            sizer->Add(new wxStaticText(dither_panel, wxID_ANY, "Dither:"), wxSizerFlags().Border(wxALL));
            wxArrayString str;
            str.Add("None");
            str.Add("Waves");
            str.Add("Floyd");
            str.Add("Horizontal");
            str.Add("Van Gogh");
            str.Add("Z1");
            str.Add("CZ332");
            str.Add("Brix");
            str.Add("Custom");
            dither_style = new wxChoice(dither_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,str);
            dither_style->SetSelection(0);
            dither_style->Bind(wxEVT_CHOICE, &frame_t::on_dither_style, this);
            sizer->Add(dither_style);

            dither_scale = new wxSlider(dither_panel, wxID_ANY, 0, 0, 40, wxDefaultPosition, wxSize(150, -1), wxSL_HORIZONTAL | wxSL_AUTOTICKS);
            dither_scale->Bind(wxEVT_SCROLL_CHANGED, &frame_t::on_dither_scale, this);
            sizer->Add(dither_scale);

            dither_cutoff = new wxSpinCtrl(dither_panel);
            dither_cutoff->SetRange(0, 48);
            dither_cutoff->SetIncrement(1);
            dither_cutoff->SetValue(model.dither_cutoff);
            dither_cutoff->Bind(wxEVT_SPINCTRL, &frame_t::on_dither_cutoff<wxSpinEvent>, this);
            dither_cutoff->Bind(wxEVT_TEXT, &frame_t::on_dither_cutoff<wxCommandEvent>, this);
            sizer->Add(dither_cutoff);

            dither_panel->SetSizer(sizer);
        }

        wxPanel* post_panel = new wxPanel(l_panel);
        {
            wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

            sizer->Add(new wxStaticText(post_panel, wxID_ANY, "Cull Dots:"), wxSizerFlags().Border(wxALL));
            cull_dots = new wxCheckBox(post_panel, wxID_ANY, "");
            cull_dots->SetValue(model.cull_dots);
            cull_dots->Bind(wxEVT_CHECKBOX, &frame_t::on_cull_dots, this);
            sizer->Add(cull_dots, wxSizerFlags().Border(wxALL));

            sizer->Add(new wxStaticText(post_panel, wxID_ANY, " Pipes:"), wxSizerFlags().Border(wxALL));
            cull_pipes = new wxCheckBox(post_panel, wxID_ANY, "");
            cull_pipes->SetValue(model.cull_pipes);
            cull_pipes->Bind(wxEVT_CHECKBOX, &frame_t::on_cull_pipes, this);
            sizer->Add(cull_pipes, wxSizerFlags().Border(wxALL));

            sizer->Add(new wxStaticText(post_panel, wxID_ANY, " Zags:"), wxSizerFlags().Border(wxALL));
            cull_zags = new wxCheckBox(post_panel, wxID_ANY, "");
            cull_zags->SetValue(model.cull_zags);
            cull_zags->Bind(wxEVT_CHECKBOX, &frame_t::on_cull_zags, this);
            sizer->Add(cull_zags, wxSizerFlags().Border(wxALL));

            sizer->Add(new wxStaticText(post_panel, wxID_ANY, " Improve Lines:"), wxSizerFlags().Border(wxALL));
            clean_lines = new wxCheckBox(post_panel, wxID_ANY, "");
            clean_lines->SetValue(model.clean_lines);
            clean_lines->Bind(wxEVT_CHECKBOX, &frame_t::on_clean_lines, this);
            sizer->Add(clean_lines, wxSizerFlags().Border(wxALL));

            post_panel->SetSizer(sizer);
        }

        {
            wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
            sizer->Add(visual, wxSizerFlags().Border(wxALL));
            sizer->Add(wh_panel, wxSizerFlags().Expand());
            sizer->Add(new wxStaticLine(l_panel));
            sizer->Add(dither_panel, wxSizerFlags().Expand());
            sizer->Add(post_panel, wxSizerFlags().Expand());

            l_panel->SetSizer(sizer);
        }

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(l_panel, wxSizerFlags().Border(wxALL));
        sizer->Add(r_panel, wxSizerFlags().Expand().Border(wxTOP));
        SetSizer(sizer);

        Bind(wxEVT_MENU, &frame_t::on_exit, this, wxID_EXIT);
        Bind(wxEVT_MENU, &frame_t::on_open, this, wxID_OPEN);
        Bind(wxEVT_MENU, &frame_t::on_save, this, wxID_SAVE);
        Bind(wxEVT_MENU, &frame_t::on_save_as, this, wxID_SAVEAS);
        Bind(wxEVT_MENU, &frame_t::on_reset, this, wxID_NEW);
        Bind(wxEVT_MENU, &frame_t::on_auto_color, this, ID_AUTO_COLOR);
        Bind(wxEVT_MENU, &frame_t::on_copy, this, wxID_COPY);
        Bind(wxEVT_MENU, &frame_t::on_paste, this, wxID_PASTE);
        Bind(wxEVT_UPDATE_UI, &frame_t::on_update, this);

        Connect(wxEVT_PAINT, wxPaintEventHandler(frame_t::on_paint), 0, this);
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        SetSize(800, 800);
        Update();
    }
 
private:
    void on_update(wxUpdateUIEvent&) 
    {
        paste->Enable(!model.display);
    }

    void on_exit(wxCommandEvent& event)
    {
        Close(false);
    }
     
    void on_open(wxCommandEvent& event)
    {
        using namespace std::filesystem;

        wxFileDialog open_dialog(
            this, _("Choose a file to open"), wxEmptyString, wxEmptyString, 
            _("Image (*.png;*.bmp)|*.png;*.bmp"),
            wxFD_OPEN, wxDefaultPosition);

        if(open_dialog.ShowModal() == wxID_OK) // if the user click "Open" instead of "Cancel"
        {
            model.base_image.LoadFile(open_dialog.GetPath());
            model.update();
            Update();
            Refresh();
        }
    }

    void on_reset(wxCommandEvent& event)
    {
        model.color_knobs = {};
        for(pal_entry_t* e : pal_entries)
            e->manual_update();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_auto_color(wxCommandEvent& event)
    {
        auto_color_dialog_t dlg(this);

        if(dlg.ShowModal() == wxID_OK)
        {
            model.auto_color(dlg.count->GetValue(), dlg.map->GetValue());
            model.update();
            for(pal_entry_t* e : pal_entries)
                e->manual_update();
            Layout();
            Update();
            Refresh();
        }
    }

    void on_save(wxCommandEvent& event)
    {
        if(!model.output_image.IsOk())
            return;

        if(model.save_path.empty())
            on_save_as(event);
        else
            do_save(model.save_path);
    }

    void on_save_as(wxCommandEvent& event)
    {
        if(!model.output_image.IsOk())
            return;

        wxFileDialog save_dialog(
            this, _("Save file as"), wxEmptyString, _("unnamed"), 
            _("PNG Image (*.png)|*.png|BMP Image (*.bmp)|*.bmp"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT, wxDefaultPosition);

        if(save_dialog.ShowModal() != wxID_CANCEL) // if the user click "Save" instead of "Cancel"
        {
            model.save_path = save_dialog.GetPath().ToStdString();
            do_save(model.save_path);
        }
    }
    void do_save(std::string filename)
    {
        if(!model.output_image.IsOk())
        {
            std::fprintf(stderr, "Unable to save %s\n", filename.c_str());
            return;
        }

        if(!model.output_image.SaveFile(filename, wxBITMAP_TYPE_PNG))
            wxLogError("Failed to save to %s", filename);
    }

    template<typename T>
    void on_change_w(T& event)
    {
        model.w = w_ctrl->GetValue();
        model.update();
        visual->resize(true);
        Layout();
        Update();
        Refresh();
    }

    template<typename T>
    void on_change_h(T& event)
    {
        model.h = h_ctrl->GetValue();
        model.update();
        visual->resize(true);
        Layout();
        Update();
        Refresh();
    }

    void on_change_display(wxCommandEvent& event)
    {
        model.display = display_checkbox->GetValue();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_cull_dots(wxCommandEvent& event)
    {
        model.cull_dots = cull_dots->GetValue();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_cull_pipes(wxCommandEvent& event)
    {
        model.cull_pipes = cull_pipes->GetValue();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_cull_zags(wxCommandEvent& event)
    {
        model.cull_zags = cull_zags->GetValue();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_clean_lines(wxCommandEvent& event)
    {
        model.clean_lines = clean_lines->GetValue();
        model.update();
        Layout();
        Update();
        Refresh();
    }

    void on_dither_style(wxCommandEvent& event)
    {
        if((dither_style_t)event.GetSelection() == DITHER_CUSTOM)
        {
            wxFileDialog open_dialog(
                this, _("Choose a dither map to open"), wxEmptyString, wxEmptyString, 
                _("Image (*.png;*.bmp)|*.png;*.bmp"),
                wxFD_OPEN, wxDefaultPosition);

            if(open_dialog.ShowModal() == wxID_OK) // if the user click "Open" instead of "Cancel"
                model.dither_images.back().LoadFile(open_dialog.GetPath());

            goto selected;
        }

        if(model.dither_style != (dither_style_t)event.GetSelection())
        {
        selected:
            model.dither_style = (dither_style_t)event.GetSelection();
            model.update();
            Layout();
            Update();
            Refresh();
        }
    }

    void on_dither_scale(wxScrollEvent& event)
    {
        if(model.dither_scale != event.GetPosition())
        {
            model.dither_scale = event.GetPosition();
            model.update();
            Layout();
            Update();
            Refresh();
        }
    }

    template<typename T>
    void on_dither_cutoff(T& event)
    {
        if(model.dither_cutoff != dither_cutoff->GetValue())
        {
            model.dither_cutoff = dither_cutoff->GetValue();
            model.update();
            Layout();
            Update();
            Refresh();
        }
    }

    void on_copy(wxCommandEvent& event)
    {
        if(wxTheClipboard && wxTheClipboard->Open())
        {
            wxBitmap& bitmap = model.display ? model.output_bitmap : model.base_bitmap;
            if(bitmap.IsOk())
            {
                wxTheClipboard->SetData(new wxBitmapDataObject(bitmap));
                wxTheClipboard->Close();
            }
            wxTheClipboard->Close();
        }
    }
    
    void on_paste(wxCommandEvent& event)
    {
        if(wxTheClipboard && wxTheClipboard->Open())
        {
            if(wxTheClipboard->IsSupported(wxDF_BITMAP))
            {
                wxBitmapDataObject data;
                if(wxTheClipboard->GetData(data))
                    model.base_image = data.GetBitmap().ConvertToImage();

                model.update();
                Layout();
                Update();
                Refresh();
            }
            wxTheClipboard->Close();
        }
    }

    wxMenuItem* copy;
    wxMenuItem* paste;

    visual_t* visual;
    wxSpinCtrl* w_ctrl;
    wxSpinCtrl* h_ctrl;
    wxCheckBox* display_checkbox;
    wxCheckBox* cull_dots;
    wxCheckBox* cull_pipes;
    wxCheckBox* cull_zags;
    wxCheckBox* clean_lines;
    std::vector<pal_entry_t*> pal_entries;

    wxChoice* dither_style;
    wxSlider* dither_scale;
    wxSpinCtrl* dither_cutoff;

    model_t model;
};

bool app_t::OnInit()
{
    wxInitAllImageHandlers();
    wxFrame* frame = new frame_t();
    frame->Show();
    frame->SendSizeEvent();
    return true;
} 


