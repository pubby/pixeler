#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/fswatcher.h>
#include <wx/bookctrl.h>
#include <wx/mstream.h>
#include <wx/clipbrd.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>

#include <filesystem>
#include <cstring>
#include <map>

#include "model.hpp"
#include "convert.hpp"
#include "id.hpp"

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
    explicit visual_t(wxWindow* parent, model_t& model, bool can_zoom = true)
    : wxScrolledWindow(parent)
    , model(model)
    {
        resize();

        SetDoubleBuffered(true);
        if(can_zoom)
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
        auto& bitmap = model.display ? model.output_bitmap : model.base_bitmap;
        if(bitmap.IsOk())
        {
#ifdef GC_RENDER
            gc.DrawBitmap(bitmap, 0, 0, model.w, model.h);
#else
            gc.DrawBitmap(bitmap, 0, 0);
        }
#endif
    }

    void resize(bool force = false)
    {
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
        if(new_scale == scale)
            return;

        scale = new_scale;

        resize(true);
        Refresh();
    }

    void on_wheel(wxMouseEvent& event)
    {
        int const rot = event.GetWheelRotation();
        int const delta = event.GetWheelDelta();
        int const turns = rot / delta;
        int const target = turns >= 0 ? (scale << turns) : (scale >> -turns);
        int const diff = target - scale;

        set_zoom(std::clamp(scale + diff, 1, 16));
    }

protected:
    model_t& model;
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
        set_color(new_color);
    }

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

class color_dialog_t : public wxDialog
{
public:
    color_dialog_t(wxWindow* parent, model_t const& model)
    : wxDialog(parent, wxID_ANY, "Color Picker")
    , model(model)
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        for(unsigned y = 0; y < 14; y += 1)
        {
            wxPanel* panel = new wxPanel(this);
            wxBoxSizer* panel_sizer = new wxBoxSizer(wxHORIZONTAL);

            for(unsigned x = 0; x < 4; x += 1)
            {
                unsigned color = x *16 + y;

                if(color == 0x1D)
                    color = 0x0F;
                else if(color == 0x0D)
                    color = 0xFF;

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
        color = new color_button_t(this, model, knob.color, &knob);
        color->Bind(wxEVT_BUTTON, &pal_entry_t::on_click, this);
        greed = new wxSlider(this, wxID_ANY, 0, -20, 20, wxDefaultPosition, wxSize(100, -1), wxSL_HORIZONTAL | wxSL_AUTOTICKS);
        greed->Bind(wxEVT_SCROLL_CHANGED, &pal_entry_t::on_greed, this);
        bleed = new wxSlider(this, wxID_ANY, 0, -10, 10, wxDefaultPosition, wxSize(100, -1), wxSL_HORIZONTAL | wxSL_AUTOTICKS);
        bleed->Bind(wxEVT_SCROLL_CHANGED, &pal_entry_t::on_bleed, this);

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(color, wxSizerFlags());
        sizer->Add(greed, wxSizerFlags());
        sizer->Add(bleed, wxSizerFlags());
        SetSizerAndFit(sizer);
    }

    void on_click(wxCommandEvent& event)
    {
        color_dialog_t dlg(this, model);
        
        if(dlg.ShowModal() == wxID_OK) 
        {
            color_button_t* btn = static_cast<color_button_t*>(event.GetEventObject());
            btn->set_color(dlg.color);
            btn->knob->color = dlg.color;
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

    //void on_open(wxCommandEvent& event);
    //void on_delete(wxCommandEvent& event);
    //void on_rename(wxCommandEvent& event);
    //unsigned index = 0;
private:
    model_t& model;
    color_knob_t& knob;
    color_button_t* color;
    wxSlider* greed;
    wxSlider* bleed;
};


class frame_t : public wxFrame
{
public:
    frame_t();
 
private:
    void on_update(wxUpdateUIEvent&) {}
    void on_paint(wxPaintEvent& event);
    void on_draw(render_t& gc);

    void on_exit(wxCommandEvent& event);

    void on_new_window(wxCommandEvent& event);
    void on_open(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_save_as(wxCommandEvent& event);
    void do_save();

    void on_watcher(wxFileSystemWatcherEvent& event)
    {
        /* TODO
        //wxFileName path = event.GetPath();

        if(event.GetChangeType() == wxFSW_EVENT_MODIFY)
        {
            try
            {
                if(std::filesystem::exists(model.collision_path))
                {
                    auto bm = load_collision_file(model.collision_path.string());
                    model.collision_bitmaps = std::move(bm.first);
                    model.collision_wx_bitmaps = std::move(bm.second);
                }
            }
            catch(...) {}

            for(auto& chr : model.chr_files)
            {
                try
                {
                    chr.load();
                }
                catch(...) {}
            }

            // TODO
            //refresh_tab();
        }
        */
    }

    void reset_watcher()
    {
        /* TODO
        if(!watcher)
        {
            watcher.reset(new wxFileSystemWatcher());
            watcher->SetOwner(this);
        }

        watcher->RemoveAll();

        auto const watch = [&](std::filesystem::path path)
        {
#ifdef __WXMSW__
            path.remove_filename();
#endif
            watcher->Add(wxString(path.string()));
        };

        watch(model.collision_path);
        for(auto const& chr : model.chr_files)
            watch(chr.path.string());
            */
    }

    template<typename T>
    void on_change_w(T& event)
    {
        model.w = w_ctrl->GetValue();
        Update();
        Refresh();
    }

    template<typename T>
    void on_change_h(T& event)
    {
        model.h = h_ctrl->GetValue();
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

    // TODO
    /*
    wxNotebook* notebook;

    chr_editor_t* chr_editor;
    palette_editor_t* palette_editor;
    metatile_panel_t* metatile_panel;
    levels_panel_t* levels_panel;
    class_panel_t* class_panel;
    */

    visual_t* visual;
    wxSpinCtrl* w_ctrl;
    wxSpinCtrl* h_ctrl;
    wxCheckBox* display_checkbox;

    model_t model;

    std::unique_ptr<wxFileSystemWatcher> watcher;
};

bool app_t::OnInit()
{
    wxInitAllImageHandlers();
    wxFrame* frame = new frame_t();
    frame->Show();
    frame->SendSizeEvent();
    return true;
} 

frame_t::frame_t()
: wxFrame(nullptr, wxID_ANY, "Pixeler", wxDefaultPosition, wxSize(800, 600))
{
    wxMenu* menu_file = new wxMenu;
    menu_file->Append(wxID_OPEN, "&Open Image\tCTRL+O");
    menu_file->Append(wxID_SAVE, "&Save Image\tCTRL+S");
    menu_file->Append(wxID_SAVEAS, "Save Image &As\tSHIFT+CTRL+S");
    menu_file->AppendSeparator();
    menu_file->Append(wxID_EXIT);

    wxMenuBar* menu_bar = new wxMenuBar;
    menu_bar->Append(menu_file, "&File");

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
        for(unsigned i = 0; i < 13; i += 1)
        {
            wxPanel* panel = new wxPanel(r_panel);

            if(i % 3 == 0)
            {
                std::string str;
                if(i == 12)
                    str = "UBC";
                else
                {
                    str += "BG" + std::to_string(i / 3) + " / ";
                    str += "SPR" + std::to_string(i / 3);
                }
                wxStaticText* label = new wxStaticText(r_panel, wxID_ANY, str.c_str());
                sizer->Add(label, wxSizerFlags());
            }

            wxBoxSizer* panel_sizer = new wxBoxSizer(wxHORIZONTAL);
            pal_entry_t*  bg_entry = new pal_entry_t(panel, model, model.color_knobs[i == 12 ? 24 : i]);
            panel_sizer->Add(bg_entry, wxSizerFlags());

            if(i != 12)
            {
                pal_entry_t* spr_entry = new pal_entry_t(panel, model, model.color_knobs[i + 12]);
                panel_sizer->Add(spr_entry, wxSizerFlags());
            }
            panel->SetSizer(panel_sizer);

            sizer->Add(panel, wxSizerFlags());
        }
        r_panel->SetSizer(sizer);
    }

    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(visual, wxSizerFlags().Border(wxALL));
        sizer->Add(wh_panel, wxSizerFlags().Expand());
        l_panel->SetSizer(sizer);
    }

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(l_panel, wxSizerFlags().Border(wxALL));
    sizer->Add(r_panel, wxSizerFlags().Expand());
    SetSizer(sizer);

    Bind(wxEVT_MENU, &frame_t::on_exit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &frame_t::on_open, this, wxID_OPEN);
    Bind(wxEVT_MENU, &frame_t::on_save, this, wxID_SAVE);
    Bind(wxEVT_MENU, &frame_t::on_save_as, this, wxID_SAVEAS);
    Bind(wxEVT_UPDATE_UI, &frame_t::on_update, this);

    Bind(wxEVT_FSWATCHER, &frame_t::on_watcher, this);

    Connect(wxEVT_PAINT, wxPaintEventHandler(frame_t::on_paint), 0, this);
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    SetSize(800, 800);
    Update();
}
 
void frame_t::on_exit(wxCommandEvent& event)
{
    Close(false);
}
 
void frame_t::on_open(wxCommandEvent& event)
{
    using namespace std::filesystem;

    wxFileDialog* open_dialog = new wxFileDialog(
        this, _("Choose a file to open"), wxEmptyString, wxEmptyString, _("PNG Image (*.png)|*.png"),
        wxFD_OPEN, wxDefaultPosition);
    auto guard = make_scope_guard([&]{ open_dialog->Destroy(); });

    if(open_dialog->ShowModal() == wxID_OK) // if the user click "Open" instead of "Cancel"
    {
        model.base_image_path = open_dialog->GetPath().ToStdString();

        FILE* fp = std::fopen(model.base_image_path.string().c_str(), "rb");
        auto guard = make_scope_guard([&]{ std::fclose(fp); });
        model.read_file(fp);

        reset_watcher();
        Update();
        Refresh();
    }
}

void frame_t::on_save(wxCommandEvent& event)
{
    /*
    if(model.project_path.empty())
        on_save_as(event);
    else
        do_save();
        */
}

void frame_t::on_save_as(wxCommandEvent& event)
{
    /*
    wxFileDialog save_dialog(
        this, _("Save file as"), wxEmptyString, _("unnamed"), 
        _("MapFab Exports (*.mapfab;*.json)|*.mapfab;*.json|MapFab Files (*.mapfab)|*.mapfab|JSON Files (*.json)|*.json"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT, wxDefaultPosition);

    if(save_dialog.ShowModal() != wxID_CANCEL) // if the user click "Save" instead of "Cancel"
    {
        model.project_path = save_dialog.GetPath().ToStdString();
        do_save();
    }
    */
}
void frame_t::do_save()
{
    /*
    using namespace std::filesystem;

    if(model.project_path.empty())
        throw std::runtime_error("Invalid file");

    path project(model.project_path);
    if(project.has_filename())
        project.remove_filename();

    FILE* fp = std::fopen(model.project_path.string().c_str(), "wb");
    auto guard = make_scope_guard([&]{ std::fclose(fp); });

    if(model.project_path.extension() == ".json")
        model.write_json(fp, model.project_path);
    else
        model.write_file(fp, model.project_path);
    model.modified_since_save = false;
    Update();
    */
}

void frame_t::on_paint(wxPaintEvent& event)
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
        //gc->Scale(scale, scale); TODO
        on_draw(*gc);
    }
#else
    wxPaintDC dc(this);
    PrepareDC(dc);
    //dc.SetUserScale(scale, scale); TODO
    on_draw(dc);
#endif
}

void frame_t::on_draw(render_t& gc)
{
    /*
    grid_box_t::on_draw(gc);

    if(!enable_tile_select())
        return;

    if(mouse_down)
    {
        gc.SetPen(wxPen(wxColor(255, 255, 255, 127), 0));
        if(mouse_down == MBTN_LEFT)
            gc.SetBrush(wxBrush(wxColor(255, 255, 0, 127)));
        else
            gc.SetBrush(wxBrush(wxColor(255, 0, 0, 127)));
        rect_t const r = rect_from_2_coords(from_screen(mouse_start), from_screen(mouse_current));
        coord_t const c0 = to_screen(r.c);
        coord_t const c1 = to_screen(r.e());
        gc.DrawRectangle(c0.x, c0.y, (c1 - c0).x, (c1 - c0).y);
    }
    */
}


