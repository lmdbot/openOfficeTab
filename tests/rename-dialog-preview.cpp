// Hidden native windows only. Render the real dialog; never starts Office.
#include "../src/rename-dialog.h"
#include "../src/frame-close-dialog.h"
#include "../src/appearance-dialog.h"
#include "../src/tab-menu.h"
#include <gdiplus.h>
#include <cassert>
#include <iostream>
class PreviewApplication final:public IDispatch {
    ULONG refs=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IDispatch)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    DISPATCH_STUBS
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID* ids)override{*ids=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID,REFIID,LCID,WORD,DISPPARAMS*,VARIANT* result,EXCEPINFO*,UINT*)override{if(result){VariantInit(result);result->vt=VT_BSTR;result->bstrVal=SysAllocString(L"Word");}return S_OK;}
};
void capture(HWND window,const wchar_t* path){
    RECT r{};GetClientRect(window,&r);HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);auto old=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,window,(POINT*)&bounds,2);
        int saved=SaveDC(dc);SetViewportOrgEx(dc,bounds.left,bounds.top,nullptr);
        IntersectClipRect(dc,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
        SendMessageW(child,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);RestoreDC(dc,saved);
    }
    Gdiplus::Bitmap image(bitmap,nullptr);CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    assert(image.Save(path,&png,nullptr)==Gdiplus::Ok);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
void showOffscreen(HWND dialog) {
    assert(dialog && !IsWindowVisible(dialog));
    // Standard controls print their content once their hierarchy is visible.
    SetWindowLongPtrW(dialog, GWL_EXSTYLE, GetWindowLongPtrW(dialog, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    SetWindowPos(dialog, nullptr, -30000, -30000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(dialog, SW_SHOWNOACTIVATE);
    UpdateWindow(dialog);
}

void checkBounds(HWND dialog) {
    RECT client{};
    GetClientRect(dialog, &client);
    for (HWND window = GetWindow(dialog, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
        RECT child{};
        GetWindowRect(window, &child);
        MapWindowPoints(nullptr, dialog, (POINT*)&child, 2);
        assert(child.left >= 0 && child.top >= 0 && child.right <= client.right && child.bottom <= client.bottom);
    }
}

void previewFrameClose(Host host, bool dark, int language, const wchar_t* path) {
    framecloseui::Choice choice{2, language, host, dark};
    framecloseui::Template data;
    HWND dialog = CreateDialogIndirectParamW(module, &data.dialog, nullptr, framecloseui::closeProc, (LPARAM)&choice);
    showOffscreen(dialog);
    checkBounds(dialog);
    auto defaultId = SendMessageW(dialog, DM_GETDEFID, 0, 0);
    assert(HIWORD(defaultId) == DC_HASDEFID && LOWORD(defaultId) == IDCANCEL);
    HWND remember = GetDlgItem(dialog, framecloseui::rememberId);
    assert(SendMessageW(remember, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
    capture(dialog, path);
    SendMessageW(remember, BM_CLICK, 0, 0);
    assert(SendMessageW(remember, BM_GETCHECK, 0, 0) == BST_CHECKED);
    DestroyWindow(dialog);
}

void previewWidth(Host host, bool dark, int language, const wchar_t* path) {
    appearanceui::WidthChoice choice{120, 2, {language, host, dark}};
    appearanceui::Template data;
    HWND dialog = CreateDialogIndirectParamW(module, &data.dialog, nullptr, appearanceui::widthProc, (LPARAM)&choice);
    showOffscreen(dialog);
    checkBounds(dialog);
    assert(SendDlgItemMessageW(dialog, appearanceui::modeId, CB_GETCOUNT, 0, 0) == 3);
    assert(SendDlgItemMessageW(dialog, appearanceui::modeId, CB_GETCURSEL, 0, 0) == 2);
    capture(dialog, path);
    SetDlgItemInt(dialog, appearanceui::editId, 240, FALSE);
    SendDlgItemMessageW(dialog, appearanceui::modeId, CB_SETCURSEL, 1, 0);
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    assert(choice.width == 240 && choice.mode == 1);
    DestroyWindow(dialog);
}

void previewColor(Host host, bool dark, int language, const wchar_t* path) {
    appearanceui::ColorChoice choice{RGB(218,234,251), false, {language, host, dark}};
    appearanceui::Template data;
    HWND dialog = CreateDialogIndirectParamW(module, &data.dialog, nullptr, appearanceui::colorProc, (LPARAM)&choice);
    showOffscreen(dialog);
    checkBounds(dialog);
    assert(!choice.custom);
    SetDlgItemTextW(dialog, appearanceui::editId, L"#DBEAFE");
    assert(choice.custom);
    capture(dialog, path);
    SendMessageW(dialog, WM_COMMAND, appearanceui::resetId, 0);
    assert(!choice.custom);
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    assert(choice.color == tabcolor::defaultBackground());
    DestroyWindow(dialog);
}

void checkTabMenu() {
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, module, nullptr);
    assert(owner);
    {
        View view;
        view.path = L"C:\\Examples\\Report.docx";
        view.locked = view.readOnly = true;
        tabmenu::Menu menu(owner, Host::Word, false, 2);
        tabmenu::populate(menu, view, 2);
        HMENU root = menu.handle();
        assert(GetMenuItemCount(root) == 13);
        assert(GetMenuState(root, 20, MF_BYCOMMAND) & MF_DISABLED);
        assert(GetMenuState(root, 5, MF_BYCOMMAND) & MF_DISABLED);
        assert(GetMenuState(root, 10, MF_BYCOMMAND) & MF_CHECKED);
        assert(!(GetMenuState(root, 11, MF_BYCOMMAND) & MF_DISABLED));
        HMENU move = GetSubMenu(root, 6), close = GetSubMenu(root, 10), more = GetSubMenu(root, 12);
        assert(move && close && more);
        assert(GetMenuItemCount(move) == 3 && GetMenuItemCount(close) == 4 && GetMenuItemCount(more) == 5);
        assert(GetMenuItemID(move, 0) == 13 && GetMenuItemID(move, 2) == 15);
        assert(GetMenuItemID(close, 0) == 6 && GetMenuItemID(close, 3) == 9);
        assert(GetMenuItemID(more, 0) == 1 && GetMenuItemID(more, 4) == 19);

        wchar_t label[48]{};
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_DATA;
        item.dwTypeData = label;
        item.cch = (UINT)std::size(label);
        assert(GetMenuItemInfoW(root, 20, FALSE, &item));
        assert(std::wstring(label) == i18n::text(2, L"重命名文件…"));
        if (item.fType & MFT_OWNERDRAW) {
            MEASUREITEMSTRUCT measure{ODT_MENU, 0, 20, 0, 0, item.dwItemData};
            assert(SendMessageW(owner, WM_MEASUREITEM, 0, (LPARAM)&measure));
            assert(measure.itemWidth > 100 && measure.itemHeight >= 30);
            HDC screen = GetDC(owner), dc = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, measure.itemWidth, measure.itemHeight);
            auto previous = SelectObject(dc, bitmap);
            DRAWITEMSTRUCT draw{ODT_MENU, 0, 20, ODA_DRAWENTIRE, ODS_DISABLED, (HWND)root, dc,
                {0, 0, (LONG)measure.itemWidth, (LONG)measure.itemHeight}, item.dwItemData};
            assert(SendMessageW(owner, WM_DRAWITEM, 0, (LPARAM)&draw));
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            ReleaseDC(owner, screen);
        }
    }
    DestroyWindow(owner);
}
int wmain(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);module=GetModuleHandleW(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    {
        Ptr<IDispatch> app(new PreviewApplication(),false);
        auto session=std::make_shared<Session>(app.get(),L"artifacts\\rename-dialog-preview-state");
        struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} data{};
        data.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN|DS_MODALFRAME;data.dialog.cx=290;data.dialog.cy=125;
        for(int mode=0;mode<3;mode++){
            session->host=mode==1?Host::Excel:Host::Word;session->settings.themeMode=mode==1?2:1;
            renameui::RenameChoice choice{session.get(),1,mode==2?L"C:\\Examples\\Quarterly report.docx":L"C:\\Examples\\项目进度报告.docx",mode==2?2:1};
            HWND dialog=CreateDialogIndirectParamW(module,&data.dialog,nullptr,renameui::renameProc,(LPARAM)&choice);
            showOffscreen(dialog);
            assert((GetWindowLongW(GetDlgItem(dialog,IDOK),GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);
            assert(SendDlgItemMessageW(dialog,101,EM_GETLIMITTEXT,0,0)==255);
            HDC fontDc=GetDC(dialog);auto oldFont=SelectObject(fontDc,choice.theme.title);wchar_t face[LF_FACESIZE]{};GetTextFaceW(fontDc,LF_FACESIZE,face);std::wcout<<L"Title font: "<<face<<L"\n";SelectObject(fontDc,oldFont);ReleaseDC(dialog,fontDc);
            checkBounds(dialog);
            capture(dialog,mode==0?L"artifacts/rename-dialog-light.png":mode==1?L"artifacts/rename-dialog-dark.png":L"artifacts/rename-dialog-en.png");
            DestroyWindow(dialog);
        }
        previewFrameClose(Host::Word, false, 1, L"artifacts/frame-close-dialog-light.png");
        previewFrameClose(Host::Excel, true, 1, L"artifacts/frame-close-dialog-dark.png");
        previewFrameClose(Host::PowerPoint, false, 2, L"artifacts/frame-close-dialog-en.png");
        previewWidth(Host::Word, false, 1, L"artifacts/tab-width-dialog-light.png");
        previewWidth(Host::Excel, true, 1, L"artifacts/tab-width-dialog-dark.png");
        previewWidth(Host::PowerPoint, false, 2, L"artifacts/tab-width-dialog-en.png");
        previewColor(Host::Word, false, 1, L"artifacts/tab-color-dialog-light.png");
        previewColor(Host::Excel, true, 1, L"artifacts/tab-color-dialog-dark.png");
        previewColor(Host::PowerPoint, false, 2, L"artifacts/tab-color-dialog-en.png");
        checkTabMenu();
    }
    Gdiplus::GdiplusShutdown(token);CoUninitialize();
    std::cout<<"Hidden dialog renders: light/dark, Chinese/English, bounds, owner-drawn default and edit limit passed.\n";
    std::cout<<"Frame close renders: light/dark, Chinese/English, bounds, cancel default and checkbox passed.\n";
    std::cout<<"Appearance dialogs render in light/dark and Chinese/English. Width/mode submission and color reset passed.\n";
    std::cout<<"Tab menu labels, submenu command IDs, disabled/checked states and native draw messages passed.\n";
}
