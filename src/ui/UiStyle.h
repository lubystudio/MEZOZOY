#pragma once

#include "Theme.h"
#include "Win32Util.h"
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>

namespace mezozoy::ui::style {

// Original Mezozoy geometry, in a 24-unit grid. No external icon assets.
enum class Icon { Home, Document, Development, Cards, Settings, Add, Right, Down,
    Folder, Character, World, Location, Reference, Lock, Unlock, Search, Undo, Redo,
    Save, History, Details, Eye, Heart, Light, Pen, Left };
inline constexpr int Radius = 6, RowHeight = 42, IconSize = 20;
inline constexpr wchar_t Hot[] = L"Mezozoy.Ui.Hot";
inline constexpr wchar_t RowHot[] = L"Mezozoy.Ui.RowHot";
inline constexpr wchar_t Active[] = L"Mezozoy.Ui.Active";
inline constexpr wchar_t ButtonIcon[] = L"Mezozoy.Ui.Icon";
inline constexpr wchar_t Tooltip[] = L"Mezozoy.Ui.Tooltip";

inline void SetIcon(HWND w, Icon icon, const wchar_t* tip) {
    SetPropW(w,ButtonIcon,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(icon)+1));
    if(!GetPropW(w,Tooltip)) {
        HWND popup=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
            0,0,0,0,w,nullptr,GetModuleHandleW(nullptr),nullptr);
        TOOLINFOW info{}; info.cbSize=sizeof(info); info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        info.hwnd=w; info.uId=reinterpret_cast<UINT_PTR>(w); info.lpszText=const_cast<wchar_t*>(tip);
        SendMessageW(popup,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
        SetPropW(w,Tooltip,reinterpret_cast<HANDLE>(popup));
    }
}

inline void Fill(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &r, brush); DeleteObject(brush);
}
inline void Surface(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius = Radius) {
    const int saved = SaveDC(dc);
    HPEN pen = CreatePen(PS_SOLID, 1, border); HBRUSH brush = CreateSolidBrush(fill);
    SelectObject(dc, pen); SelectObject(dc, brush);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    RestoreDC(dc, saved); DeleteObject(pen); DeleteObject(brush);
}
inline void DrawIcon(HDC dc, Icon icon, RECT r, COLORREF color) {
    const int saved = SaveDC(dc);
    const double s = std::min(r.right-r.left, r.bottom-r.top) / 24.0;
    auto x = [&](int v) { return r.left + static_cast<int>(v*s+.5); };
    auto y = [&](int v) { return r.top + static_cast<int>(v*s+.5); };
    HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(2.1*s+.5)), color);
    SelectObject(dc, pen); SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    auto line = [&](int a,int b,int c,int d) { MoveToEx(dc,x(a),y(b),nullptr); LineTo(dc,x(c),y(d)); };
    auto box = [&](int a,int b,int c,int d) { RoundRect(dc,x(a),y(b),x(c),y(d),x(3)-x(0),y(3)-y(0)); };
    auto circle = [&](int a,int b,int c,int d) { Ellipse(dc,x(a),y(b),x(c),y(d)); };
    auto curve = [&](int a,int b,int c,int d,int e,int f,int g,int h) {
        POINT points[]={{x(a),y(b)},{x(c),y(d)},{x(e),y(f)},{x(g),y(h)}}; PolyBezier(dc,points,4);
    };
    switch(icon) {
    case Icon::Home: line(3,11,12,3); line(12,3,21,11); line(6,9,6,21); line(6,21,18,21); line(18,21,18,9); line(10,21,10,15); line(10,15,14,15); line(14,15,14,21); break;
    case Icon::Document: box(5,3,19,22); line(8,8,16,8); line(8,12,16,12); line(8,16,14,16); break;
    case Icon::Cards: box(3,3,10,10); box(14,3,21,10); box(3,14,10,21); box(14,14,21,21); break;
    case Icon::Add: line(12,5,12,19); line(5,12,19,12); break;
    case Icon::Right: line(9,6,15,12); line(15,12,9,18); break;
    case Icon::Left: line(15,6,9,12); line(9,12,15,18); break;
    case Icon::Down: line(6,9,12,15); line(12,15,18,9); break;
    case Icon::Folder: line(3,8,3,5); line(3,5,10,5); line(10,5,13,8); box(3,8,22,21); break;
    case Icon::Character: circle(8,3,16,11); curve(4,21,4,11,20,11,20,21); line(4,21,20,21); break;
    case Icon::World: circle(3,3,21,21); Ellipse(dc,x(8),y(3),x(16),y(21)); line(3,12,21,12); break;
    case Icon::Location: circle(9,6,15,12); curve(5,14,-3,-2,27,-2,19,14); line(5,14,12,22); line(12,22,19,14); break;
    case Icon::Reference: box(3,3,21,21); circle(14,6,18,10); line(4,18,10,11); line(10,11,15,17); line(15,17,18,14); line(18,14,21,18); break;
    case Icon::Development: circle(3,3,9,9); circle(15,3,21,9); circle(9,16,15,22); line(6,9,12,16); line(18,9,12,16); break;
    case Icon::Settings: circle(5,5,19,19); circle(10,10,14,14); line(12,2,12,5); line(12,19,12,22); line(2,12,5,12); line(19,12,22,12); line(4,4,7,7); line(17,17,20,20); line(4,20,7,17); line(17,7,20,4); break;
    case Icon::Search: circle(3,3,17,17); line(15,15,22,22); break;
    case Icon::Lock: case Icon::Unlock: box(5,11,20,22); curve(8,8,8,1,18,1,18,8); line(8,8,8,11); if(icon==Icon::Lock) line(18,8,18,11); line(12,15,12,18); break;
    case Icon::Undo: line(8,4,3,9); line(3,9,8,14); line(3,9,15,9); Arc(dc,x(10),y(9),x(22),y(21),x(16),y(21),x(16),y(9)); break;
    case Icon::Redo: line(16,4,21,9); line(21,9,16,14); line(21,9,9,9); Arc(dc,x(2),y(9),x(14),y(21),x(8),y(9),x(8),y(21)); break;
    case Icon::Save: box(3,3,21,21); box(7,3,17,10); box(7,14,17,21); break;
    case Icon::History: circle(3,3,21,21); line(12,6,12,12); line(12,12,17,15); break;
    case Icon::Details: for(int yy : {6,12,18}) { line(3,yy,5,yy); line(9,yy,21,yy); } break;
    case Icon::Eye: Arc(dc,x(2),y(5),x(22),y(21),x(2),y(13),x(22),y(13)); Arc(dc,x(2),y(3),x(22),y(19),x(22),y(11),x(2),y(11)); circle(9,9,15,15); break;
    case Icon::Heart: line(3,9,12,21); line(12,21,21,9); Arc(dc,x(3),y(3),x(12),y(15),x(3),y(9),x(12),y(9)); Arc(dc,x(12),y(3),x(21),y(15),x(12),y(9),x(21),y(9)); break;
    case Icon::Light: circle(6,3,18,15); line(9,15,9,19); line(9,19,15,19); line(15,19,15,15); line(10,22,14,22); break;
    case Icon::Pen: line(4,16,16,4); line(16,4,20,8); line(20,8,8,20); line(8,20,3,21); line(3,21,4,16); line(13,7,17,11); break;
    }
    RestoreDC(dc,saved); DeleteObject(pen);
}
inline Icon CaptionIcon(const std::wstring& text) {
    if (text.empty()) return Icon::Document;
    switch(text[0]) {
    case L'\u265F': return Icon::Character; case L'\u2302': return Icon::Location;
    case L'\u25C8': return Icon::World; case L'\u25A7': return Icon::Reference;
    case L'\u25A1': return Icon::Folder; case L'\u265C': return Icon::History;
    case L'\u2637': case L'\u2630': return Icon::Details; case L'\u2668': return Icon::Heart;
    case L'\u25CF': return Icon::Light; case L'\u270E': return Icon::Pen;
    default: return Icon::Document;
    }
}
inline std::wstring CleanCaption(std::wstring text) {
    if (!text.empty() && text.front() > 0x2000) {
        const auto start = text.find_first_not_of(L" ", 1);
        if (start != std::wstring::npos) text.erase(0,start);
    }
    return text;
}
inline RECT IconBounds(RECT r, int size=IconSize) {
    const int x=(r.left+r.right-size)/2, y=(r.top+r.bottom-size)/2;
    return {x,y,x+size,y+size};
}
// Retain native hit testing, keyboard access and thumb tracking; paint only the chrome.
inline void Scrollbars(HWND w, const Theme& t, HDC supplied=nullptr) {
    const auto bits=GetWindowLongPtrW(w,GWL_STYLE);
    if(!(bits&(WS_VSCROLL|WS_HSCROLL))) return;
    if (!IsWindowVisible(w)) return;
    HDC dc=supplied?supplied:GetDCEx(w,nullptr,DCX_WINDOW|DCX_CACHE|DCX_CLIPSIBLINGS); if(!dc) return;
    const int savedDc=SaveDC(dc);
    RECT window{}; GetWindowRect(w,&window);
    RECT bounds{0,0,window.right-window.left,window.bottom-window.top};
    IntersectClipRect(dc,0,0,bounds.right,bounds.bottom);
    RECT client{}; GetClientRect(w,&client);
    POINT origin{}; ClientToScreen(w,&origin);
    OffsetRect(&client,origin.x-window.left,origin.y-window.top);
    // Nonclient chrome must never paint into labels, edits or adjacent siblings.
    ExcludeClipRect(dc,client.left,client.top,client.right,client.bottom);
    for(bool vertical : {true,false}) {
        if(!(bits&(vertical?WS_VSCROLL:WS_HSCROLL))) continue;
        SCROLLBARINFO info{}; info.cbSize=sizeof(info);
        if(!GetScrollBarInfo(w,vertical?OBJID_VSCROLL:OBJID_HSCROLL,&info) || (info.rgstate[0]&STATE_SYSTEM_INVISIBLE)) continue;
        RECT bar=info.rcScrollBar; OffsetRect(&bar,-window.left,-window.top);
        RECT clipped{}; if(!IntersectRect(&clipped,&bar,&bounds)) continue;
        const int barDc=SaveDC(dc);
        IntersectClipRect(dc,clipped.left,clipped.top,clipped.right,clipped.bottom);
        Fill(dc,bar,t.background);
        RECT thumb=bar;
        if(vertical) { thumb.left+=5; thumb.right-=5; thumb.top=bar.top+info.xyThumbTop; thumb.bottom=bar.top+info.xyThumbBottom; }
        else { thumb.top+=5; thumb.bottom-=5; thumb.left=bar.left+info.xyThumbTop; thumb.right=bar.left+info.xyThumbBottom; }
        const auto color=Theme::Blend(t.border,t.textMuted,25);
        if(thumb.right>thumb.left && thumb.bottom>thumb.top) Surface(dc,thumb,color,color,3);
        const int saved=SaveDC(dc); HPEN pen=CreatePen(PS_SOLID,1,t.textMuted); SelectObject(dc,pen);
        if(vertical) {
            int x=(bar.left+bar.right)/2, y=bar.top+info.dxyLineButton/2;
            MoveToEx(dc,x-3,y+1,nullptr); LineTo(dc,x,y-2); LineTo(dc,x+3,y+1);
            y=bar.bottom-info.dxyLineButton/2;
            MoveToEx(dc,x-3,y-1,nullptr); LineTo(dc,x,y+2); LineTo(dc,x+3,y-1);
        }
        RestoreDC(dc,saved); DeleteObject(pen);
        RestoreDC(dc,barDc);
    }
    RestoreDC(dc,savedDc);
    if(!supplied) ReleaseDC(w,dc);
}
inline LRESULT CALLBACK Interaction(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    const auto* theme = reinterpret_cast<const Theme*>(data);
    if (msg==WM_MOUSEMOVE) {
        if(!GetPropW(w,Hot)) { SetPropW(w,Hot,reinterpret_cast<HANDLE>(1)); TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,w,0}; TrackMouseEvent(&t); InvalidateRect(w,nullptr,FALSE); }
        wchar_t cls[32]{}; GetClassNameW(w,cls,32);
        if(_wcsicmp(cls,WC_TREEVIEWW)==0) {
            TVHITTESTINFO hit{}; hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            HANDLE row=reinterpret_cast<HANDLE>(TreeView_HitTest(w,&hit));
            if(GetPropW(w,RowHot)!=row) { SetPropW(w,RowHot,row); InvalidateRect(w,nullptr,FALSE); }
        } else if(_wcsicmp(cls,L"ListBox")==0 && GetPropW(w,L"Mezozoy.Ui.List")) {
            const auto hit=static_cast<DWORD>(SendMessageW(w,LB_ITEMFROMPOINT,0,lp));
            HANDLE row=HIWORD(hit)?nullptr:reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(LOWORD(hit))+1);
            if(GetPropW(w,RowHot)!=row) { SetPropW(w,RowHot,row); InvalidateRect(w,nullptr,FALSE); }
        }
    }
    if(msg==WM_MOUSELEAVE) { RemovePropW(w,Hot); RemovePropW(w,RowHot); InvalidateRect(w,nullptr,FALSE); }
    if(msg==WM_NCDESTROY) {
        if(HWND tip=reinterpret_cast<HWND>(RemovePropW(w,Tooltip))) DestroyWindow(tip);
        RemovePropW(w,ButtonIcon); RemovePropW(w,L"Mezozoy.Ui.List");
        RemovePropW(w,Hot); RemovePropW(w,RowHot); RemovePropW(w,Active); RemoveWindowSubclass(w,Interaction,id);
    }
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) { InvalidateRect(w,nullptr,FALSE); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME); }
    if (theme && (msg==WM_PAINT || msg==WM_PRINTCLIENT)) {
        wchar_t cls[32]{}; GetClassNameW(w,cls,32);
        if(_wcsicmp(cls,L"ComboBox")==0 && (GetWindowLongPtrW(w,GWL_STYLE)&3)>=CBS_DROPDOWN) {
            PAINTSTRUCT ps{}; HDC dc=msg==WM_PAINT?BeginPaint(w,&ps):reinterpret_cast<HDC>(wp); RECT r{}; GetClientRect(w,&r);
            const int saved=SaveDC(dc);
            COMBOBOXINFO combo{}; combo.cbSize=sizeof(combo); GetComboBoxInfo(w,&combo);
            const bool editable=(GetWindowLongPtrW(w,GWL_STYLE)&3)==CBS_DROPDOWN;
            if(editable) ExcludeClipRect(dc,combo.rcItem.left,combo.rcItem.top,combo.rcItem.right,combo.rcItem.bottom);
            Fill(dc,r,theme->background);
            Surface(dc,r,theme->page,GetFocus()==w?theme->accent:theme->border);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,theme->text); SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0)));
            RECT text=r; text.left+=10; text.right-=28;
            const auto caption=WindowText(w); if(!editable) DrawTextW(dc,caption.c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            RECT arrow{r.right-25,(r.bottom-18)/2,r.right-7,(r.bottom+18)/2}; DrawIcon(dc,Icon::Down,arrow,theme->textMuted);
            RestoreDC(dc,saved);
            if(msg==WM_PAINT) EndPaint(w,&ps); return 0;
        }
    }
    const LRESULT result=DefSubclassProc(w,msg,wp,lp);
    if(theme && (msg==WM_NCPAINT || msg==WM_VSCROLL || msg==WM_HSCROLL || msg==WM_NCMOUSEMOVE || msg==WM_MOUSEWHEEL)) Scrollbars(w,*theme);
    if(theme && msg==WM_PRINT) Scrollbars(w,*theme,reinterpret_cast<HDC>(wp));
    if(theme && msg==WM_NCPAINT && (GetWindowLongPtrW(w,GWL_STYLE)&WS_BORDER)) {
        HDC dc=GetWindowDC(w); RECT r{}; GetWindowRect(w,&r); OffsetRect(&r,-r.left,-r.top);
        HBRUSH brush=CreateSolidBrush(GetFocus()==w?theme->accent:theme->border); FrameRect(dc,&r,brush); DeleteObject(brush); ReleaseDC(w,dc);
    }
    return result;
}
inline void Attach(HWND w,const Theme* theme) {
    SetWindowSubclass(w,Interaction,9601,reinterpret_cast<DWORD_PTR>(theme));
}
inline void Button(DRAWITEMSTRUCT* d,const Theme& t,COLORREF backdrop=CLR_INVALID) {
    const bool pressed=d->itemState&ODS_SELECTED, disabled=d->itemState&ODS_DISABLED;
    const bool active=GetPropW(d->hwndItem,Active)!=nullptr, hot=GetPropW(d->hwndItem,Hot)!=nullptr;
    Fill(d->hDC,d->rcItem,backdrop==CLR_INVALID?t.background:backdrop);
    const COLORREF fill=active?t.selection:pressed?Theme::Blend(t.panelAlt,t.accent,18):hot?Theme::Blend(t.panelAlt,t.text,6):t.panel;
    Surface(d->hDC,d->rcItem,fill,active || (d->itemState&ODS_FOCUS)?t.accent:t.border);
    const auto caption=WindowText(d->hwndItem);
    const COLORREF text=disabled?t.textMuted:t.text;
    if(auto icon=reinterpret_cast<ULONG_PTR>(GetPropW(d->hwndItem,ButtonIcon))) {
        DrawIcon(d->hDC,static_cast<Icon>(icon-1),IconBounds(d->rcItem),active?t.accent:text);
    } else if(caption==L"+" || caption==L"›" || caption==L"‹") {
        if(caption==L"‹") { RECT r=IconBounds(d->rcItem); DrawIcon(d->hDC,Icon::Left,r,text); }
        else DrawIcon(d->hDC,caption==L"+"?Icon::Add:Icon::Right,IconBounds(d->rcItem),text);
    } else {
        SelectObject(d->hDC,reinterpret_cast<HFONT>(SendMessageW(d->hwndItem,WM_GETFONT,0,0)));
        SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,text); RECT r=d->rcItem; r.left+=8; r.right-=8;
        DrawTextW(d->hDC,caption.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
}
inline void ListRow(DRAWITEMSTRUCT* d,const Theme& t) {
    Fill(d->hDC,d->rcItem,t.navigation);
    if(d->itemID==static_cast<UINT>(-1)) return;
    const bool active=d->itemState&ODS_SELECTED;
    const bool hot=reinterpret_cast<ULONG_PTR>(GetPropW(d->hwndItem,RowHot))==d->itemID+1;
    RECT r=d->rcItem; InflateRect(&r,-8,-2);
    if(active) Surface(d->hDC,r,t.selection,Theme::Blend(t.navigation,t.accent,40));
    else if(hot) Surface(d->hDC,r,t.panelAlt,t.panelAlt);
    const int length=static_cast<int>(SendMessageW(d->hwndItem,LB_GETTEXTLEN,d->itemID,0));
    if(length<0) return;
    std::wstring raw(length+1,L'\0'); SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,reinterpret_cast<LPARAM>(raw.data())); raw.resize(length);
    const Icon icons[]={Icon::Document,Icon::History,Icon::Details,Icon::Eye,Icon::Heart,Icon::Light,Icon::Pen};
    const Icon settingsIcons[]={Icon::Eye,Icon::Pen,Icon::Folder,Icon::Save,Icon::Settings,Icon::Details};
    RECT icon{r.left+10,r.top+(r.bottom-r.top-20)/2,r.left+30,r.top+(r.bottom-r.top+20)/2};
    DrawIcon(d->hDC,GetDlgCtrlID(d->hwndItem)==5101?settingsIcons[d->itemID%6]:icons[d->itemID%7],icon,active?t.accent:t.textMuted);
    r.left+=42; r.right-=8; const auto text=CleanCaption(raw);
    SelectObject(d->hDC,reinterpret_cast<HFONT>(SendMessageW(d->hwndItem,WM_GETFONT,0,0))); SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,active?t.text:t.textMuted);
    DrawTextW(d->hDC,text.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
}
inline LRESULT Tree(NMTVCUSTOMDRAW* d,const Theme& t,HFONT normal,HFONT bold) {
    if(d->nmcd.dwDrawStage==CDDS_PREPAINT) {
        // TreeView can request just one row while retaining the other rows in
        // its buffer. Clearing the full client here erases those retained rows.
        return CDRF_NOTIFYITEMDRAW;
    }
    if(d->nmcd.dwDrawStage!=CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    HWND w=d->nmcd.hdr.hwndFrom; auto item=reinterpret_cast<HTREEITEM>(d->nmcd.dwItemSpec);
    RECT client{}; GetClientRect(w,&client); RECT row=d->nmcd.rc; row.left=0; row.right=client.right;
    if(row.bottom<=0 || row.top>=client.bottom) return CDRF_SKIPDEFAULT;
    const int saved=SaveDC(d->nmcd.hdc);
    IntersectClipRect(d->nmcd.hdc,client.left,client.top,client.right,client.bottom);
    Fill(d->nmcd.hdc,row,t.navigation);
    bool selected=TreeView_GetSelection(w)==item, hot=GetPropW(w,RowHot)==reinterpret_cast<HANDLE>(item);
    RECT bg=row; InflateRect(&bg,-4,-2);
    if(selected || hot) Surface(d->nmcd.hdc,bg,selected?t.selection:t.panelAlt,selected?Theme::Blend(t.navigation,t.accent,40):t.panelAlt);
    wchar_t value[1024]{}; TVITEMW info{}; info.mask=TVIF_TEXT|TVIF_STATE|TVIF_CHILDREN; info.hItem=item; info.pszText=value; info.cchTextMax=1024; info.stateMask=TVIS_BOLD|TVIS_EXPANDED; TreeView_GetItem(w,&info);
    RECT label{}; TreeView_GetItemRect(w,item,&label,TRUE);
    const int center=(row.top+row.bottom)/2;
    if(info.cChildren) DrawIcon(d->nmcd.hdc,info.state&TVIS_EXPANDED?Icon::Down:Icon::Right,{label.left-19,center-8,label.left-3,center+8},t.textMuted);
    DrawIcon(d->nmcd.hdc,CaptionIcon(value),{label.left+3,center-9,label.left+21,center+9},selected?t.accent:t.textMuted);
    label.left+=30; label.right=client.right-38; label.top=row.top; label.bottom=row.bottom;
    SelectObject(d->nmcd.hdc,info.state&TVIS_BOLD?bold:normal); SetBkMode(d->nmcd.hdc,TRANSPARENT); SetTextColor(d->nmcd.hdc,selected || (info.state&TVIS_BOLD)?t.text:t.textMuted);
    const auto text=CleanCaption(value); DrawTextW(d->nmcd.hdc,text.empty()?L"Без названия":text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    RestoreDC(d->nmcd.hdc,saved);
    return CDRF_SKIPDEFAULT;
}
} // namespace mezozoy::ui::style
