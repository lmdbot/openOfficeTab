#pragma once
#include "dialog-theme.h"

// Native controls retain text editing, accessibility and dialog keyboard behavior.
namespace renameui {
struct RenameChoice {
    Session* session;unsigned long long id;std::wstring path;int language;
    dialogui::Theme theme{};
};
inline void layout(HWND dialog, RenameChoice& choice) {
    auto& c = choice.theme;
    c.initialize(dialog, choice.session ? choice.session->host : Host::Word,
        choice.session && choice.session->darkMode(), choice.language);
    c.applyFonts(dialog, 110);
    auto move=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(dialog,id),c.px(x),c.px(y),c.px(w),c.px(h),TRUE);};
    move(110,80,23,376,30);move(111,80,57,376,22);move(112,24,103,432,22);
    // Extension has its own read-only area; it is never part of the editable stem.
    auto suffix=documentrename::extension(choice.path);HDC dc=GetDC(dialog);auto old=SelectObject(dc,c.body);SIZE size{};GetTextExtentPoint32W(dc,suffix.c_str(),(int)suffix.size(),&size);SelectObject(dc,old);ReleaseDC(dialog,dc);
    int badge=std::clamp(MulDiv(size.cx,96,c.scale)+24,54,112);
    move(101,38,139,432-badge-28,24);move(113,456-badge,139,badge-12,24);
    move(114,24,184,432,38);move(IDCANCEL,256,238,92,34);move(IDOK,360,238,96,34);
    InvalidateRect(dialog,nullptr,TRUE);
}
inline INT_PTR CALLBACK renameProc(HWND dialog,UINT message,WPARAM wp,LPARAM lp){
    auto choice=(RenameChoice*)GetWindowLongPtrW(dialog,DWLP_USER);
    if(message==WM_INITDIALOG){
        choice=(RenameChoice*)lp;SetWindowLongPtrW(dialog,DWLP_USER,lp);
        SetWindowTextW(dialog,L"openOfficeTab");
        auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,dialog,(HMENU)(INT_PTR)id,module,nullptr);};
        control(L"STATIC",i18n::text(choice->language,L"重命名文件"),SS_NOPREFIX,110);
        control(L"STATIC",documentrename::leaf(choice->path).c_str(),SS_PATHELLIPSIS|SS_NOPREFIX,111);
        control(L"STATIC",i18n::text(choice->language,L"新文件名"),SS_NOPREFIX,112);
        auto edit=control(L"EDIT",documentrename::stem(choice->path).c_str(),WS_TABSTOP|ES_AUTOHSCROLL,101);
        SendMessageW(edit,EM_SETLIMITTEXT,255,0);SetWindowSubclass(edit,dialogui::controlProc,1,0);
        control(L"STATIC",documentrename::extension(choice->path).c_str(),SS_RIGHT|SS_ENDELLIPSIS|SS_NOPREFIX,113);
        control(L"STATIC",i18n::text(choice->language,L"保留原目录和扩展名；当前编辑会一并保存。"),SS_NOPREFIX,114);
        for(int id:{IDCANCEL,IDOK}){auto button=control(L"BUTTON",i18n::text(choice->language,id==IDOK?L"重命名":L"取消"),WS_TABSTOP|BS_OWNERDRAW,id);SetWindowSubclass(button,dialogui::controlProc,1,0);}
        layout(dialog,*choice);
        choice->theme.center(dialog, 480, 288);
        choice->theme.caption(dialog);
        SendMessageW(dialog,DM_SETDEFID,IDOK,0);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);return FALSE;
    }
    if(!choice)return FALSE;
    auto& c=choice->theme;
    if(message==WM_ERASEBKGND)return TRUE;
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT){
        int id = GetDlgCtrlID((HWND)lp);
        return c.textColors((HDC)wp, id == 111 || id == 113 || id == 114, id == 101 || id == 113);
    }
    if(message==WM_PAINT||message==WM_PRINTCLIENT){
        PAINTSTRUCT ps{};HDC dc=message==WM_PAINT?BeginPaint(dialog,&ps):(HDC)wp;
        c.paintSurface(dialog, dc, 224);
        auto border=c.rect(24,130,432,44);dialogui::rounded(dc,border,c.field,GetFocus()==GetDlgItem(dialog,101)?c.accent:c.border,c.px(12),c.px(GetFocus()==GetDlgItem(dialog,101)?2:1));
        auto icon=c.rect(24,24,40,48);dialogui::rounded(dc,icon,c.footer,c.border,c.px(8));
        auto pen=CreatePen(PS_SOLID,c.px(2),c.accent);auto oldPen=SelectObject(dc,pen);
        MoveToEx(dc,c.px(35),c.px(40),nullptr);LineTo(dc,c.px(53),c.px(40));MoveToEx(dc,c.px(35),c.px(47),nullptr);LineTo(dc,c.px(49),c.px(47));MoveToEx(dc,c.px(35),c.px(54),nullptr);LineTo(dc,c.px(46),c.px(54));SelectObject(dc,oldPen);DeleteObject(pen);
        if(message==WM_PAINT)EndPaint(dialog,&ps);return TRUE;
    }
    if(message==WM_DRAWITEM&&(wp==IDOK||wp==IDCANCEL)){
        c.paintButton(*(DRAWITEMSTRUCT*)lp, wp == IDOK);
        return TRUE;
    }
    if(message==WM_DPICHANGED){auto r=(RECT*)lp;SetWindowPos(dialog,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout(dialog,*choice);return TRUE;}
    if(message==WM_COMMAND&&LOWORD(wp)==IDOK){
        wchar_t name[256]{};GetDlgItemTextW(dialog,101,name,256);
        auto alert=[&](const wchar_t* key){MessageBoxW(dialog,i18n::text(choice->language,key),i18n::text(choice->language,L"重命名文件"),MB_OK|MB_ICONINFORMATION);};
        if(!documentrename::validStem(name,documentrename::extension(choice->path))){alert(L"文件名无效。请勿使用保留名称、路径字符或末尾的空格和句点。");return TRUE;}
        auto view=choice->session->find(choice->id);
        if(!view||view->path!=choice->path){alert(L"文档路径已改变，请关闭对话框后重试。");return TRUE;}
        EnableWindow(dialog,FALSE);auto result=choice->session->renameFile(choice->id,name);EnableWindow(dialog,TRUE);
        if(result==documentrename::Result::failed){alert(L"未完成重命名，原文件未被删除。请检查同名文件、只读状态或 Office 提示；仅修改大小写暂不支持。");return TRUE;}
        if(result==documentrename::Result::originalRetained)alert(L"文档已保存为新名称，但旧文件无法安全删除，已保留两份文件。请检查旧文件后手动处理。");
        EndDialog(dialog,IDOK);return TRUE;
    }
    if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL)){EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
}
