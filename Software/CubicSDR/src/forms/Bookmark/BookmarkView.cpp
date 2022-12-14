// Copyright (c) Charles J. Cliffe
// SPDX-License-Identifier: GPL-2.0+

#include <wx/menu.h>
#include <wx/textdlg.h>

#include <algorithm>

#include "BookmarkView.h"
#include "CubicSDR.h"
#include "ActionDialog.h"


#define wxCONTEXT_ADD_GROUP_ID 1000

#define BOOKMARK_VIEW_CHOICE_DEFAULT "Bookmark.."
#define BOOKMARK_VIEW_CHOICE_MOVE "Move to.."
#define BOOKMARK_VIEW_CHOICE_NEW "(New Group..)"

#define BOOKMARK_VIEW_STR_ADD_GROUP "Add Group"
#define BOOKMARK_VIEW_STR_ADD_GROUP_DESC "Enter Group Name"
#define BOOKMARK_VIEW_STR_UNNAMED "Unnamed"
#define BOOKMARK_VIEW_STR_CLEAR_RECENT "Clear Recents"
#define BOOKMARK_VIEW_STR_RENAME_GROUP "Rename Group"


BookmarkViewVisualDragItem::BookmarkViewVisualDragItem(const wxString& labelValue) : wxDialog(nullptr, wxID_ANY, L"", wxPoint(20,20), wxSize(-1,-1), wxFRAME_TOOL_WINDOW | wxNO_BORDER | wxSTAY_ON_TOP | wxALL ) {
    
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *label = new wxStaticText( this, wxID_ANY, labelValue, wxDefaultPosition, wxDefaultSize, wxEXPAND );
    
    sizer->Add(label, 1, wxALL | wxEXPAND, 5);
    
    SetSizerAndFit(sizer);
    Show();
}

class ActionDialogRemoveBookmark : public ActionDialog {
public:
    explicit ActionDialogRemoveBookmark( BookmarkEntryPtr be ) : ActionDialog(wxGetApp().getAppFrame(), wxID_ANY, wxT("Remove Bookmark?")) {
        subject = be;
        m_questionText->SetLabelText(wxT("Are you sure you want to remove the bookmark\n '" + BookmarkMgr::getBookmarkEntryDisplayName(subject) + "'?"));
    }
    
    void doClickOK() override {
        wxGetApp().getBookmarkMgr().removeBookmark(subject);
        wxGetApp().getBookmarkMgr().updateBookmarks();
    }

private:
    BookmarkEntryPtr subject;
};

class ActionDialogRemoveGroup : public ActionDialog {
public:
    explicit ActionDialogRemoveGroup( std::string groupName ) : ActionDialog(wxGetApp().getAppFrame(), wxID_ANY, wxT("Remove Group?")) {
        subject = groupName;
        m_questionText->SetLabelText(wxT("Warning: Are you sure you want to remove the group\n '" + subject + "' AND ALL BOOKMARKS WITHIN IT?"));
    }
    
    void doClickOK() override {
        wxGetApp().getBookmarkMgr().removeGroup(subject);
        wxGetApp().getBookmarkMgr().updateBookmarks();
    }
    
private:
    std::string subject;
};


class ActionDialogRemoveRange : public ActionDialog {
public:
    explicit ActionDialogRemoveRange( const BookmarkRangeEntryPtr& rangeEnt ) : ActionDialog(wxGetApp().getAppFrame(), wxID_ANY, wxT("Remove Range?")) {
        subject = rangeEnt;
        
        std::wstring name = rangeEnt->label;
        
        if (name.length() == 0) {
            std::string wstr = frequencyToStr(rangeEnt->startFreq) + " - " + frequencyToStr(rangeEnt->endFreq);

            name = wxString(wstr).ToStdWstring();
        }
        
        m_questionText->SetLabelText(L"Are you sure you want to remove the range\n '" + name + L"'?");
    }
    
    void doClickOK() override {
        wxGetApp().getBookmarkMgr().removeRange(subject);
        wxGetApp().getBookmarkMgr().updateActiveList();
    }
    
private:
    BookmarkRangeEntryPtr subject;
};


class ActionDialogUpdateRange : public ActionDialog {
public:
    explicit ActionDialogUpdateRange( const BookmarkRangeEntryPtr& rangeEnt ) : ActionDialog(wxGetApp().getAppFrame(), wxID_ANY, wxT("Update Range?")) {
        subject = rangeEnt;
        
        std::wstring name = rangeEnt->label;
        
        if (name.length() == 0) {
            std::string wstr = frequencyToStr(rangeEnt->startFreq) + " - " + frequencyToStr(rangeEnt->endFreq);
            
			name = wxString(wstr).ToStdWstring();
        }
        
        m_questionText->SetLabelText(L"Are you sure you want to update the range\n '" + name + L"' to the active range?");
    }
    
    void doClickOK() override {
        BookmarkRangeEntryPtr ue = BookmarkView::makeActiveRangeEntry();

        subject->freq = ue->freq;
        subject->startFreq = ue->startFreq;
        subject->endFreq = ue->endFreq;
          
        wxGetApp().getBookmarkMgr().updateActiveList();
    }
    
private:
    BookmarkRangeEntryPtr subject;
};




BookmarkView::BookmarkView( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) : BookmarkPanel(parent, id, pos, size, style) {

    rootBranch = m_treeView->AddRoot("Root");
    activeBranch = m_treeView->AppendItem(rootBranch, "Active");
    rangeBranch = m_treeView->AppendItem(rootBranch, "View Ranges");
    bookmarkBranch = m_treeView->AppendItem(rootBranch, "Bookmarks");
    recentBranch = m_treeView->AppendItem(rootBranch, "Recents");
    
    expandState["active"] = true;
    expandState["range"] = false;
    expandState["bookmark"] = true;
    expandState["recent"] = true;
    
    doUpdateActive.store(true);
    doUpdateBookmarks.store(true);
    bookmarkChoice = nullptr;
    dragItem = nullptr;
    dragItemId = nullptr;

    m_clearSearchButton->Hide();
    hideProps();
    
    m_updateTimer.Start(500);
   
    visualDragItem = nullptr;
    nextEnt = nullptr;
    nextDemod = nullptr;
    nextGroup = "";

    mouseTracker.setTarget(this);
}

BookmarkView::~BookmarkView() {

    dragItem = nullptr;
    dragItemId = nullptr;

    visualDragItem = nullptr;
    nextEnt = nullptr;
    nextDemod = nullptr;
}

void BookmarkView::onUpdateTimer( wxTimerEvent& /* event */ ) {
    if (!this->IsShown()) {
        return;
    }
    if (doUpdateActive.load()) {
        doUpdateActiveList();
        doUpdateActive.store(false);
    }
    if (doUpdateBookmarks.load()) {

        wxTreeItemId bmSel = refreshBookmarks();
        if (bmSel) {
            m_treeView->SelectItem(bmSel);
        }  
        doUpdateBookmarks.store(false);  
    }
}

bool BookmarkView::skipEvents() {

    return !this->IsShown() || doUpdateActive || doUpdateBookmarks;
}

void BookmarkView::updateTheme() {
    wxColour bgColor(ThemeMgr::mgr.currentTheme->generalBackground);
    wxColour textColor(ThemeMgr::mgr.currentTheme->text);
    wxColour btn(ThemeMgr::mgr.currentTheme->button);
    wxColour btnHl(ThemeMgr::mgr.currentTheme->buttonHighlight);
    
    SetBackgroundColour(bgColor);

    m_treeView->SetBackgroundColour(bgColor);
    m_treeView->SetForegroundColour(textColor);
    
    m_propPanel->SetBackgroundColour(bgColor);
    m_propPanel->SetForegroundColour(textColor);

    m_buttonPanel->SetBackgroundColour(bgColor);
    m_buttonPanel->SetForegroundColour(textColor);
    
    m_labelLabel->SetForegroundColour(textColor);
    m_frequencyVal->SetForegroundColour(textColor);
    m_frequencyLabel->SetForegroundColour(textColor);
    m_bandwidthVal->SetForegroundColour(textColor);
    m_bandwidthLabel->SetForegroundColour(textColor);
    m_modulationVal->SetForegroundColour(textColor);
    m_modulationLabel->SetForegroundColour(textColor);
    
    refreshLayout();
}


void BookmarkView::updateActiveList() {
    doUpdateActive.store(true);
}


void BookmarkView::updateBookmarks() {
    doUpdateBookmarks.store(true);
}


void BookmarkView::updateBookmarks(const std::string& group) {
    doUpdateBookmarkGroup.insert(group);
    doUpdateBookmarks.store(true);
}

bool BookmarkView::isKeywordMatch(std::wstring search_str, std::vector<std::wstring> &keywords) {
    wstring str = search_str;
    std::transform(str.begin(), str.end(), str.begin(), towlower);

    for (const auto& k : keywords) {
        if (str.find(k) == wstring::npos) {
            return false;
        }
    }
    
    return true;
}

wxTreeItemId BookmarkView::refreshBookmarks() {
    
    //capture the previously selected item info BY COPY (because the original will be destroyed together with the destroyed tree items) to restore it again after 
    //having rebuilding the whole tree.
    TreeViewItem* prevSel = itemToTVI(m_treeView->GetSelection());
    TreeViewItem* prevSelCopy = nullptr;

    if (prevSel != nullptr) {
        prevSelCopy = new TreeViewItem(*prevSel);
    }

    BookmarkNames groupNameList;
    wxGetApp().getBookmarkMgr().getGroups(groupNameList);
    
    doUpdateBookmarkGroup.clear();
   
    wxTreeItemId bmSelFound = nullptr;
    
    std::map<std::string, bool> groupExpandState;
    bool searchState = !searchKeywords.empty();
    
    groups.clear();

    m_treeView->DeleteChildren(bookmarkBranch);

    bool bmExpandState = expandState["bookmark"];

    for (const auto& gn_i : groupNameList) {
        auto* tvi = new TreeViewItem();
        tvi->type = TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP;
        tvi->groupName = gn_i;
        wxTreeItemId group_itm = m_treeView->AppendItem(bookmarkBranch, gn_i);
        SetTreeItemData(group_itm, tvi);
        groups[gn_i] = group_itm;
        if (prevSelCopy != nullptr && prevSelCopy->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP && gn_i == prevSelCopy->groupName) {
            bmSelFound = group_itm;
        } else if (!nextGroup.empty() && gn_i == nextGroup) {
            bmSelFound = group_itm;
            nextGroup = "";
        }
    }

    if (searchState || bmExpandState) {
        m_treeView->Expand(bookmarkBranch);
    } else {
        m_treeView->Collapse(bookmarkBranch);
    }

    for (const auto& gn_i : groupNameList) {
        wxTreeItemId groupItem = groups[gn_i];

        bool groupExpanded = searchState || wxGetApp().getBookmarkMgr().getExpandState(gn_i);

        BookmarkList bmList = wxGetApp().getBookmarkMgr().getBookmarks(gn_i);

        for (auto &bmEnt : bmList) {
            std::wstring labelVal = BookmarkMgr::getBookmarkEntryDisplayName(bmEnt);

            if (searchState) {
                std::string freqStr = frequencyToStr(bmEnt->frequency);
                std::string bwStr = frequencyToStr(bmEnt->bandwidth);

                std::wstring fullText = labelVal +
                    L" " + bmEnt->label +
                    L" " + std::to_wstring(bmEnt->frequency) +
                    L" " + wxString(freqStr).ToStdWstring() +
                    L" " + wxString(bwStr).ToStdWstring() +
                    L" " + wxString(bmEnt->type).ToStdWstring();
                
                if (!isKeywordMatch(fullText, searchKeywords)) {
                    continue;
                }
            }
            
            auto* tvi = new TreeViewItem();
            tvi->type = TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK;
            tvi->bookmarkEnt = bmEnt;
            tvi->groupName = gn_i;
            
            wxTreeItemId itm = m_treeView->AppendItem(groupItem, labelVal);
            SetTreeItemData(itm, tvi);
            if (prevSelCopy != nullptr && prevSelCopy->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK && prevSelCopy->bookmarkEnt == bmEnt && groupExpanded) {
                bmSelFound = itm;
            }
            if (nextEnt == bmEnt) {
                bmSelFound = itm;
                nextEnt = nullptr;
            }
        }

        if (groupExpanded) {
            m_treeView->Expand(groupItem);
        }
    }

    delete prevSelCopy;

    return bmSelFound;
}


void BookmarkView::doUpdateActiveList() {

    auto demods = wxGetApp().getDemodMgr().getDemodulators();
    auto lastActiveDemodulator = wxGetApp().getDemodMgr().getCurrentModem();

    //capture the previously selected item info BY COPY (because the original will be destroyed together with the destroyed tree items) to restore it again after 
    //having rebuilding the whole tree.
    TreeViewItem* prevSel = itemToTVI(m_treeView->GetSelection());
    TreeViewItem* prevSelCopy = nullptr;
   
    if (prevSel != nullptr) {
        prevSelCopy = new TreeViewItem(*prevSel);
    }

    // Actives
    m_treeView->DeleteChildren(activeBranch);
    
    bool activeExpandState = expandState["active"];
    bool searchState = !searchKeywords.empty();
    
    wxTreeItemId selItem = nullptr;
    for (const auto& demod_i : demods) {
        wxString activeLabel = BookmarkMgr::getActiveDisplayName(demod_i);
        
        if (searchState) {
            std::string freqStr = frequencyToStr(demod_i->getFrequency());
            std::string bwStr = frequencyToStr(demod_i->getBandwidth());
            std::string mtype = demod_i->getDemodulatorType();
            
            std::wstring fullText = activeLabel.ToStdWstring() +
            L" " + demod_i->getDemodulatorUserLabel() +
            L" " + std::to_wstring(demod_i->getFrequency()) +
            L" " + wxString(freqStr).ToStdWstring() +
            L" " + wxString(bwStr).ToStdWstring() +
            L" " + wxString(mtype).ToStdWstring();
            
            if (!isKeywordMatch(fullText, searchKeywords)) {
                continue;
            }
        }

        auto* tvi = new TreeViewItem();
        tvi->type = TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE;
        tvi->demod = demod_i;

        wxTreeItemId itm = m_treeView->AppendItem(activeBranch,activeLabel);
        SetTreeItemData(itm, tvi);
        
        if (nextDemod != nullptr && nextDemod == demod_i) {
            selItem = itm;
            nextDemod = nullptr;
        } else if (!selItem && activeExpandState && lastActiveDemodulator && lastActiveDemodulator == demod_i) {
            selItem = itm;
        }
    }

    bool rangeExpandState = !searchState && expandState["range"];
    
	//Ranges
    BookmarkRangeList bmRanges = wxGetApp().getBookmarkMgr().getRanges();

    m_treeView->DeleteChildren(rangeBranch);
    
    for (auto &re_i: bmRanges) {
        auto* tvi = new TreeViewItem();
        tvi->type = TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE;
        tvi->rangeEnt = re_i;
        
        std::wstring labelVal = re_i->label;
        
        if (labelVal.empty()) {
            std::string wstr = frequencyToStr(re_i->startFreq) + " - " + frequencyToStr(re_i->endFreq);

            labelVal = wxString(wstr).ToStdWstring();
        }
        
        wxTreeItemId itm = m_treeView->AppendItem(rangeBranch, labelVal);
        SetTreeItemData(itm, tvi);
        
        if (nextRange == re_i) {
            selItem = itm;
            nextRange = nullptr;
        } else if (!selItem && rangeExpandState && prevSelCopy && prevSelCopy->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE && prevSelCopy->rangeEnt == re_i) {
            selItem = itm;
        }
    }
     
    bool recentExpandState = searchState || expandState["recent"];
    
    // Recents
    BookmarkList bmRecents = wxGetApp().getBookmarkMgr().getRecents();
    
	m_treeView->DeleteChildren(recentBranch);
    
    for (auto &bmr_i: bmRecents) {
        auto* tvi = new TreeViewItem();
        tvi->type = TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT;
        tvi->bookmarkEnt = bmr_i;

        std::wstring labelVal;
        bmr_i->node->child("user_label")->element()->get(labelVal);

        if (labelVal.empty()) {
            std::string str = frequencyToStr(bmr_i->frequency) + " " + bmr_i->type;

            labelVal = wxString(str).ToStdWstring();
        }
        
        if (!searchKeywords.empty()) {
            
            std::string freqStr = frequencyToStr(bmr_i->frequency);
            std::string bwStr = frequencyToStr(bmr_i->bandwidth);
            
            std::wstring fullText = labelVal +
                L" " + std::to_wstring(bmr_i->frequency) +

                L" " + wxString(freqStr).ToStdWstring() +
                L" " + wxString(bwStr).ToStdWstring() +
                L" " + wxString(bmr_i->type).ToStdWstring();
            
            if (!isKeywordMatch(fullText, searchKeywords)) {
                continue;
            }
        }
        
        wxTreeItemId itm = m_treeView->AppendItem(recentBranch, labelVal);
        SetTreeItemData(itm, tvi);

        if (nextEnt == bmr_i) {
            selItem = itm;
            nextEnt = nullptr;
        } else if (!selItem && recentExpandState && prevSelCopy && prevSelCopy->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT && prevSelCopy->bookmarkEnt == bmr_i) {
            selItem = itm;
        }
    }
    
    if (activeExpandState) {
        m_treeView->Expand(activeBranch);
    } else {
        m_treeView->Collapse(activeBranch);
    }
    if (recentExpandState) {
        m_treeView->Expand(recentBranch);
    } else {
        m_treeView->Collapse(recentBranch);
    }
    if (rangeExpandState) {
        m_treeView->Expand(rangeBranch);
    } else {
        m_treeView->Collapse(rangeBranch);
    }

    //select the item having the same meaning as the previously selected item
    if (selItem != nullptr) {
        m_treeView->SelectItem(selItem);
    }

    // Add an extra refresh, that rebuilds the buttons from sratch.
    activeSelection(lastActiveDemodulator);

    delete prevSelCopy;
}


void BookmarkView::onKeyUp( wxKeyEvent& event ) {
    // Check for active selection
    wxTreeItemId itm = m_treeView->GetSelection();

    if (itm == nullptr) {
        event.Skip();
        return;
    }

    // Create event to pass to appropriate function
    wxTreeEvent treeEvent;
    treeEvent.SetItem(itm);

    // Pull TreeViewItem data
    auto tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(itm));

    // Not selected?
    if (tvi == nullptr) {
        event.Skip();
        return;
    }

    // Handlers
    if (event.m_keyCode == WXK_DELETE || event.m_keyCode == WXK_NUMPAD_DELETE) {
        if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
            onRemoveActive(treeEvent);
        } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
            onRemoveRecent(treeEvent);
        } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
            onRemoveBookmark(treeEvent);
        } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
            onRemoveRange(treeEvent);
        } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
            onRemoveGroup(treeEvent);
        }

        // TODO: keys for other actions?
    }
}


void BookmarkView::onTreeActivate( wxTreeEvent& event ) {

    wxTreeItemId itm = event.GetItem();
    auto* tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(itm));

    if (tvi == nullptr) {
        event.Skip();
        return;
    }

    if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        if (!tvi->demod->isActive()) {
            wxGetApp().setFrequency(tvi->demod->getFrequency());
            nextDemod = tvi->demod;
            wxGetApp().getDemodMgr().setActiveDemodulator(nextDemod, false);
        }
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {

        nextEnt = tvi->bookmarkEnt;
        wxGetApp().getBookmarkMgr().removeRecent(tvi->bookmarkEnt);

        activateBookmark(tvi->bookmarkEnt);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        activateBookmark(tvi->bookmarkEnt);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
        activateRange(tvi->rangeEnt);
    }
}


void BookmarkView::onTreeCollapse( wxTreeEvent& event ) {
    
    bool searchState = !searchKeywords.empty();
    
    if (searchState) {
        event.Skip();
        return;
    }
    
    if (event.GetItem() == activeBranch) {
        expandState["active"] = false;
    } else if (event.GetItem() == bookmarkBranch) {
        expandState["bookmark"] = false;
    } else if (event.GetItem() == recentBranch) {
        expandState["recent"] = false;
    } else if (event.GetItem() == rangeBranch) {
        expandState["range"] = false;
    } else {
        TreeViewItem *tvi = itemToTVI(event.GetItem());
        
        if (tvi != nullptr) {
            if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
                wxGetApp().getBookmarkMgr().setExpandState(tvi->groupName,false);
            }
        }

    }
}


void BookmarkView::onTreeExpanded( wxTreeEvent& event ) {

    bool searchState = !searchKeywords.empty();
    
    if (searchState) {
        event.Skip();
        return;
    }
    
    if (event.GetItem() == activeBranch) {
        expandState["active"] = true;
    } else if (event.GetItem() == bookmarkBranch) {
        expandState["bookmark"] = true;
    } else if (event.GetItem() == recentBranch) {
        expandState["recent"] = true;
    } else if (event.GetItem() == rangeBranch) {
        expandState["range"] = true;
    } else {
        TreeViewItem *tvi = itemToTVI(event.GetItem());
        
        if (tvi != nullptr) {
            if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
                wxGetApp().getBookmarkMgr().setExpandState(tvi->groupName,true);
            }
        }
        
    }
}


void BookmarkView::onTreeItemMenu( wxTreeEvent& /* event */ ) {

    if (m_treeView->GetSelection() == bookmarkBranch) {
        wxMenu menu;
        menu.Append(wxCONTEXT_ADD_GROUP_ID, BOOKMARK_VIEW_STR_ADD_GROUP);
        menu.Connect(wxCONTEXT_ADD_GROUP_ID, wxEVT_MENU, wxCommandEventHandler(BookmarkView::onMenuItem), nullptr, this);
        PopupMenu(&menu);
    }
}


void BookmarkView::onMenuItem(wxCommandEvent& event) {

    if (event.GetId() == wxCONTEXT_ADD_GROUP_ID) {
        onAddGroup(event);
    }
}


bool BookmarkView::isMouseInView() {
    if (m_labelText->HasFocus()) {
        return true;
    }
    if (m_searchText->HasFocus()) {
        return true;
    }
    return  mouseTracker.mouseInView();
}


bool BookmarkView::getExpandState(const std::string& branchName) {
    return expandState[branchName];
}


void BookmarkView::setExpandState(const std::string& branchName, bool state) {
    expandState[branchName] = state;
}


void BookmarkView::ensureSelectionInView() {
    // Ensure current selection is visible; useful when a layout action
    // may have covered the active selection

    auto sel = m_treeView->GetSelection();
    if (sel != nullptr) {
        if (!m_treeView->IsVisible(sel)) {
            m_treeView->EnsureVisible(sel);
        }
    }
}

void BookmarkView::hideProps(bool hidePanel) {
    m_frequencyLabel->Hide();
    m_frequencyVal->Hide();
    
    m_bandwidthLabel->Hide();
    m_bandwidthVal->Hide();
    
    m_modulationVal->Hide();
    m_modulationLabel->Hide();
    
    m_labelText->Hide();
    m_labelLabel->Hide();

    if (hidePanel) {
        m_propPanelDivider->Hide();
        m_propPanel->Hide();
        m_buttonPanel->Hide();
    }
}


void BookmarkView::showProps() {
    m_propPanelDivider->Show();
    m_propPanel->Show();
}


void BookmarkView::clearButtons() {
    m_buttonPanel->Hide();
    m_buttonPanel->DestroyChildren();
    bookmarkChoice = nullptr;
}

void BookmarkView::showButtons() {
    m_buttonPanel->Show();
}

void BookmarkView::refreshLayout() {
    GetSizer()->Layout();
    ensureSelectionInView();
}


wxButton *BookmarkView::makeButton(wxWindow * /* parent */, const std::string& labelVal, wxObjectEventFunction handler) {
    auto *nButton = new wxButton( m_buttonPanel, wxID_ANY, labelVal);
    nButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, handler, nullptr, this);

    return nButton;
}


wxButton *BookmarkView::addButton(wxWindow *parent, const std::string& labelVal, wxObjectEventFunction handler) {
    wxButton *nButton = makeButton(parent, labelVal, handler);
    parent->GetSizer()->Add( nButton, 0, wxEXPAND);
    return nButton;
}


void BookmarkView::doBookmarkActive(const std::string& group, const DemodulatorInstancePtr& demod) {

    wxGetApp().getBookmarkMgr().addBookmark(group, demod);
    wxGetApp().getBookmarkMgr().updateBookmarks();
}


void BookmarkView::doBookmarkRecent(const std::string& group, const BookmarkEntryPtr& be) {
    
    wxGetApp().getBookmarkMgr().addBookmark(group, be);
    nextEnt = be;
    wxGetApp().getBookmarkMgr().removeRecent(be);
    wxGetApp().getBookmarkMgr().updateBookmarks();
    bookmarkSelection(be);
}


void BookmarkView::doMoveBookmark(const BookmarkEntryPtr& be, const std::string& group) {
    wxGetApp().getBookmarkMgr().moveBookmark(be, group);
    nextEnt = be;
    wxGetApp().getBookmarkMgr().updateBookmarks();
    bookmarkSelection(be);
}


void BookmarkView::doRemoveActive(const DemodulatorInstancePtr& demod) {

	wxGetApp().getBookmarkMgr().removeActive(demod);
	wxGetApp().getBookmarkMgr().updateActiveList();
}


void BookmarkView::doRemoveRecent(const BookmarkEntryPtr& be) {
    wxGetApp().getBookmarkMgr().removeRecent(be);
    wxGetApp().getBookmarkMgr().updateActiveList();
}

void BookmarkView::doClearRecents() {
    wxGetApp().getBookmarkMgr().clearRecents();
    wxGetApp().getBookmarkMgr().updateActiveList();
}


void BookmarkView::updateBookmarkChoices() {

    bookmarkChoices.clear();

    TreeViewItem *activeSel = itemToTVI(m_treeView->GetSelection());
    
    bookmarkChoices.push_back(((activeSel != nullptr && activeSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK))?BOOKMARK_VIEW_CHOICE_MOVE:BOOKMARK_VIEW_CHOICE_DEFAULT);
    wxGetApp().getBookmarkMgr().getGroups(bookmarkChoices);
    bookmarkChoices.push_back(BOOKMARK_VIEW_CHOICE_NEW);
}

void BookmarkView::addBookmarkChoice(wxWindow *parent) {
    updateBookmarkChoices();
    bookmarkChoice = new wxChoice(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, bookmarkChoices, wxEXPAND, wxDefaultValidator, "Bookmark");
    bookmarkChoice->Select(0);
    bookmarkChoice->Connect( wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(BookmarkView::onBookmarkChoice), nullptr, this);
    parent->GetSizer()->Add(bookmarkChoice, 0, wxALL | wxEXPAND);
}


void BookmarkView::onBookmarkChoice( wxCommandEvent & /* event */ ) {
   
    TreeViewItem *tvi = itemToTVI(m_treeView->GetSelection());
    
    int numSel = bookmarkChoice->GetCount();
    int sel = bookmarkChoice->GetSelection();
    
    if (sel == 0) {
        return;
    }
    
    wxString stringVal = "";
    
    if (sel == (numSel-1)) {
        stringVal = wxGetTextFromUser(BOOKMARK_VIEW_STR_ADD_GROUP_DESC, BOOKMARK_VIEW_STR_ADD_GROUP, "");
    } else {
        stringVal = bookmarkChoices[sel];
    }
    
    if (stringVal.empty()) {
        return;
    }

    if (tvi != nullptr) {
        if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
            doBookmarkActive(stringVal.ToStdString(), tvi->demod);
        }
        if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
            doBookmarkRecent(stringVal.ToStdString(), tvi->bookmarkEnt);
        }
        if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
            doMoveBookmark(tvi->bookmarkEnt, stringVal.ToStdString());
        }
    }
}

void BookmarkView::activeSelection(const DemodulatorInstancePtr& dsel) {
   
    if (dsel == nullptr) {
        hideProps();
        clearButtons();
        refreshLayout();
        return;
    }

    m_frequencyVal->SetLabelText(frequencyToStr(dsel->getFrequency()));
    m_bandwidthVal->SetLabelText(frequencyToStr(dsel->getBandwidth()));
    m_modulationVal->SetLabelText(dsel->getDemodulatorType());
    m_labelText->SetValue(dsel->getDemodulatorUserLabel());
    
    hideProps(false);

    m_frequencyVal->Show();
    m_frequencyLabel->Show();
    
    m_bandwidthVal->Show();
    m_bandwidthLabel->Show();
    
    m_modulationVal->Show();
    m_modulationLabel->Show();
    
    m_labelText->Show();
    m_labelLabel->Show();
    
    clearButtons();

    addBookmarkChoice(m_buttonPanel);
    
    if (!(dsel->isRecording())) {
         addButton(m_buttonPanel, "Start Recording", wxCommandEventHandler(BookmarkView::onStartRecording));
     } else {
         addButton(m_buttonPanel, "Stop Recording", wxCommandEventHandler(BookmarkView::onStopRecording));
     }
   
    addButton(m_buttonPanel, "Remove Active", wxCommandEventHandler( BookmarkView::onRemoveActive ));
    
    showProps();
    showButtons();
    refreshLayout();
}


void BookmarkView::activateBookmark(const BookmarkEntryPtr& bmEnt) {

	wxTreeItemId selItem = m_treeView->GetSelection();
	if (selItem) {
		m_treeView->SelectItem(selItem, false);
	}

	//if a matching DemodulatorInstance do not exist yet, create it and activate it, else use
	//the already existing one:
	// we search among the list of existing demodulators the one matching 
	//bmEnt and activate it. The search is made backwards, to select the most recently created one.
	DemodulatorInstancePtr matchingDemod = wxGetApp().getDemodMgr().getLastDemodulatorWith(
																		bmEnt->type,
																		bmEnt->label, 
																		bmEnt->frequency, 
																		bmEnt->bandwidth);
	//not found, create a new demod instance: 
	if (matchingDemod == nullptr) {

		matchingDemod = wxGetApp().getDemodMgr().loadInstance(bmEnt->node);
		matchingDemod->run();
		wxGetApp().notifyDemodulatorsChanged();
	}

	matchingDemod->setActive(true);

	long long freq = matchingDemod->getFrequency();
	long long currentFreq = wxGetApp().getFrequency();
	long long currentRate = wxGetApp().getSampleRate();

	if ((abs(freq - currentFreq) > currentRate / 2) || (abs(currentFreq - freq) > currentRate / 2)) {
		wxGetApp().setFrequency(freq);
	}

	nextDemod = matchingDemod;
    wxGetApp().getDemodMgr().setActiveDemodulator(nextDemod, false);

    //wxGetApp().getBookmarkMgr().updateActiveList();
}


void BookmarkView::activateRange(const BookmarkRangeEntryPtr& rangeEnt) {
    
	//the following only works if rangeEnt->freq is the middle of [rangeEnt->startFreq ; rangeEnt->startFreq]
	wxGetApp().setFrequency(rangeEnt->freq);
	
	// Change View limits to fit the range exactly.
    wxGetApp().getAppFrame()->setViewState(rangeEnt->startFreq + (rangeEnt->endFreq - rangeEnt->startFreq) / 2, rangeEnt->endFreq - rangeEnt->startFreq);
}


void BookmarkView::bookmarkSelection(const BookmarkEntryPtr& bmSel) {
    
    m_frequencyVal->SetLabelText(frequencyToStr(bmSel->frequency));
    m_bandwidthVal->SetLabelText(frequencyToStr(bmSel->bandwidth));
    m_modulationVal->SetLabelText(bmSel->type);
    m_labelText->SetValue(bmSel->label);
    
    hideProps(false);
    
    m_frequencyVal->Show();
    m_frequencyLabel->Show();
    
    m_bandwidthVal->Show();
    m_bandwidthLabel->Show();
    
    m_modulationVal->Show();
    m_modulationLabel->Show();
    
    m_labelText->Show();
    m_labelLabel->Show();
    
    clearButtons();
    
    addBookmarkChoice(m_buttonPanel);
    addButton(m_buttonPanel, "Activate Bookmark", wxCommandEventHandler( BookmarkView::onActivateBookmark ));
    addButton(m_buttonPanel, "Remove Bookmark", wxCommandEventHandler( BookmarkView::onRemoveBookmark ));
   
    showProps();
    showButtons();
    refreshLayout();
}


void BookmarkView::recentSelection(const BookmarkEntryPtr& bmSel) {
    
    m_frequencyVal->SetLabelText(frequencyToStr(bmSel->frequency));
    m_bandwidthVal->SetLabelText(frequencyToStr(bmSel->bandwidth));
    m_modulationVal->SetLabelText(bmSel->type);
    m_labelText->SetValue(bmSel->label);
    
    hideProps(false);

    m_frequencyVal->Show();
    m_frequencyLabel->Show();
    
    m_bandwidthVal->Show();
    m_bandwidthLabel->Show();
    
    m_modulationVal->Show();
    m_modulationLabel->Show();
    
    m_labelText->Show();
    m_labelLabel->Show();
    
    clearButtons();
    
    addBookmarkChoice(m_buttonPanel);
    addButton(m_buttonPanel, "Activate Recent", wxCommandEventHandler( BookmarkView::onActivateRecent ));
    addButton(m_buttonPanel, "Remove Recent", wxCommandEventHandler( BookmarkView::onRemoveRecent ));
    
    showProps();
    showButtons();
    refreshLayout();
}

void BookmarkView::groupSelection(const std::string& groupName) {
    
    clearButtons();
    
    hideProps(false);

    m_labelText->SetValue(groupName);
    
    m_labelText->Show();
    m_labelLabel->Show();
    
    addButton(m_buttonPanel, "Remove Group", wxCommandEventHandler( BookmarkView::onRemoveGroup ));
    
    showProps();
    
    showButtons();
    refreshLayout();
}


void BookmarkView::rangeSelection(const BookmarkRangeEntryPtr& re) {
    
    clearButtons();
    hideProps(false);

    m_labelText->SetValue(re->label);
    
    m_labelText->Show();
    m_labelLabel->Show();

    m_frequencyVal->Show();
    m_frequencyLabel->Show();

    std::string strFreq = frequencyToStr(re->startFreq) + "-" + frequencyToStr(re->endFreq);
    
    m_frequencyVal->SetLabelText(wxString(strFreq));
    
    showProps();

    addButton(m_buttonPanel, "Go to Range", wxCommandEventHandler( BookmarkView::onActivateRange ));
    addButton(m_buttonPanel, "Update Range", wxCommandEventHandler( BookmarkView::onUpdateRange ))->SetToolTip("Update range by setting it to the active range.");
    addButton(m_buttonPanel, "Remove Range", wxCommandEventHandler( BookmarkView::onRemoveRange ));
    
    showButtons();
    refreshLayout();
}


void BookmarkView::bookmarkBranchSelection() {
    
    clearButtons();
    hideProps();

    addButton(m_buttonPanel, BOOKMARK_VIEW_STR_ADD_GROUP, wxCommandEventHandler( BookmarkView::onAddGroup ));
    
    showButtons();
    refreshLayout();
}


void BookmarkView::recentBranchSelection() {
    clearButtons();
    hideProps();

    addButton(m_buttonPanel, BOOKMARK_VIEW_STR_CLEAR_RECENT, wxCommandEventHandler( BookmarkView::onClearRecents ));
    
    showButtons();
    refreshLayout();
}


void BookmarkView::rangeBranchSelection() {
    clearButtons();
    hideProps(false);

    m_labelText->SetValue(wxT(""));
    m_labelText->Show();
    m_labelLabel->Show();

    showProps();

    addButton(m_buttonPanel, "Add Active Range", wxCommandEventHandler( BookmarkView::onAddRange ));
    
    showButtons();
    refreshLayout();
}


void BookmarkView::activeBranchSelection() {
    hideProps();
    refreshLayout();
}


void BookmarkView::onTreeSelect( wxTreeEvent& event ) {

    if (skipEvents()) {
        return;
    }

    wxTreeItemId itm = event.GetItem();
    auto* tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(itm));

    if (!tvi) {
        
        if (itm == bookmarkBranch) {
            bookmarkBranchSelection();
        } else if (itm == activeBranch) {
            activeBranchSelection();
        } else if (itm == recentBranch) {
            recentBranchSelection();
        } else if (itm == rangeBranch) {
            rangeBranchSelection();
        } else {
            hideProps();
            refreshLayout();
        }
        
        return;
    }
                                                    
    if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        activeSelection(tvi->demod);
        if (tvi->demod->isActive()) {
            wxGetApp().getDemodMgr().setActiveDemodulator(nullptr, true);
            wxGetApp().getDemodMgr().setActiveDemodulator(tvi->demod, false);
            tvi->demod->setTracking(true);
        }
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
        recentSelection(tvi->bookmarkEnt);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        bookmarkSelection(tvi->bookmarkEnt);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
        groupSelection(tvi->groupName);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
        rangeSelection(tvi->rangeEnt);
    } else {
        hideProps();
        refreshLayout();
    }
}


void BookmarkView::onTreeSelectChanging( wxTreeEvent& event ) {

    event.Skip();
}


void BookmarkView::onLabelKillFocus(wxFocusEvent &event) {
    event.Skip();

    wxCommandEvent dummyEvt;
    onLabelText(dummyEvt);
}

void BookmarkView::onLabelText( wxCommandEvent& /* event */ ) {

    std::wstring newLabel = m_labelText->GetValue().ToStdWstring();
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel != nullptr) {
        if (curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
            curSel->demod->setDemodulatorUserLabel(newLabel);
            wxGetApp().getBookmarkMgr().updateActiveList();
        } else if (curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
            curSel->bookmarkEnt->label = m_labelText->GetValue().ToStdWstring();
            curSel->bookmarkEnt->node->child("user_label")->element()->set(newLabel);
            wxGetApp().getBookmarkMgr().updateBookmarks();
        } else if (curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
            curSel->bookmarkEnt->label = m_labelText->GetValue().ToStdWstring();
            curSel->bookmarkEnt->node->child("user_label")->element()->set(newLabel);
            wxGetApp().getBookmarkMgr().updateActiveList();
        } else if (curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
            curSel->rangeEnt->label = m_labelText->GetValue().ToStdWstring();
            wxGetApp().getBookmarkMgr().updateActiveList();
        } else if (curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
            std::string newGroupName = m_labelText->GetValue().ToStdString();

            if (!newGroupName.empty() && newGroupName != curSel->groupName) {
                wxGetApp().getBookmarkMgr().renameGroup(curSel->groupName, newGroupName);
                nextGroup = newGroupName;
                wxGetApp().getBookmarkMgr().updateBookmarks();
            }
        }
    }
    
    if (!m_treeView->HasFocus()) { m_treeView->SetFocus(); }
}


void BookmarkView::onDoubleClickFreq( wxMouseEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        wxGetApp().getDemodMgr().setActiveDemodulator(nullptr, true);
        wxGetApp().getDemodMgr().setActiveDemodulator(curSel->demod, false);
        wxGetApp().showFrequencyInput(FrequencyDialog::FrequencyDialogTarget::FDIALOG_TARGET_DEFAULT);
    }
}


void BookmarkView::onDoubleClickBandwidth( wxMouseEvent& /* event */ ) {
    
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        wxGetApp().getDemodMgr().setActiveDemodulator(nullptr, true);
        wxGetApp().getDemodMgr().setActiveDemodulator(curSel->demod, false);
        wxGetApp().showFrequencyInput(FrequencyDialog::FrequencyDialogTarget::FDIALOG_TARGET_BANDWIDTH);
    }
}


void BookmarkView::onRemoveActive( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        doRemoveActive(curSel->demod);
    }
}

void BookmarkView::onStartRecording( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        if (!curSel->demod->isRecording() && wxGetApp().getConfig()->verifyRecordingPath()) {
            curSel->demod->setRecording(true);
          
            wxGetApp().getBookmarkMgr().updateActiveList();
        }
    }
}


void BookmarkView::onStopRecording( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        if (curSel->demod->isRecording()) {
            curSel->demod->setRecording(false);
           
            wxGetApp().getBookmarkMgr().updateActiveList();
        }
    }
}



void BookmarkView::onRemoveBookmark( wxCommandEvent& /* event */ ) {
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        ActionDialog::showDialog(new ActionDialogRemoveBookmark(curSel->bookmarkEnt));
    }
}


void BookmarkView::onActivateBookmark( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        activateBookmark(curSel->bookmarkEnt);
    }
}


void BookmarkView::onActivateRecent( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
        BookmarkEntryPtr bookmarkEntToActivate = curSel->bookmarkEnt;
      
        //let removeRecent() + activateBookmark() refresh the tree 
        //and delete the recent node properly...
        wxGetApp().getBookmarkMgr().removeRecent(bookmarkEntToActivate);

        activateBookmark(bookmarkEntToActivate);   
    }
}


void BookmarkView::onRemoveRecent ( wxCommandEvent& /* event */ ) {
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
        BookmarkEntryPtr bookmarkEntToRemove = curSel->bookmarkEnt;
        
        //let removeRecent() + updateActiveList() refresh the tree 
        //and delete the recent node properly...
        doRemoveRecent(bookmarkEntToRemove);
    }
}

void BookmarkView::onClearRecents ( wxCommandEvent& /* event */ ) {
    doClearRecents();
}


void BookmarkView::onAddGroup( wxCommandEvent& /* event */ ) {

    wxString stringVal = wxGetTextFromUser(BOOKMARK_VIEW_STR_ADD_GROUP_DESC, BOOKMARK_VIEW_STR_ADD_GROUP, "");
    if (!stringVal.ToStdString().empty()) {
        wxGetApp().getBookmarkMgr().addGroup(stringVal.ToStdString());
        wxGetApp().getBookmarkMgr().updateBookmarks();
    }
}


void BookmarkView::onRemoveGroup( wxCommandEvent& /* event */ ) {
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());

    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
        ActionDialog::showDialog(new ActionDialogRemoveGroup(curSel->groupName));
    }
}


void BookmarkView::onAddRange( wxCommandEvent& /* event */ ) {
    
    BookmarkRangeEntryPtr re = BookmarkView::makeActiveRangeEntry();
    
    re->label = m_labelText->GetValue();

    wxGetApp().getBookmarkMgr().addRange(re);
    wxGetApp().getBookmarkMgr().updateActiveList();
}


void BookmarkView::onRemoveRange( wxCommandEvent& /* event */ ) {
    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
        ActionDialog::showDialog(new ActionDialogRemoveRange(curSel->rangeEnt));
    }
}


void BookmarkView::onRenameRange( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (!curSel || curSel->type != TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP) {
        return;
    }
    
    wxString stringVal = "";
    stringVal = wxGetTextFromUser(BOOKMARK_VIEW_STR_RENAME_GROUP, "New Group Name", curSel->groupName);
    
    std::string newGroupName = stringVal.Trim().ToStdString();
    
    if (!newGroupName.empty()) {
        wxGetApp().getBookmarkMgr().renameGroup(curSel->groupName, newGroupName);
        wxGetApp().getBookmarkMgr().updateBookmarks();
    }
}

void BookmarkView::onActivateRange( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
        activateRange(curSel->rangeEnt);
    }
}

void BookmarkView::onUpdateRange( wxCommandEvent& /* event */ ) {

    TreeViewItem *curSel = itemToTVI(m_treeView->GetSelection());
    
    if (curSel && curSel->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RANGE) {
        ActionDialog::showDialog(new ActionDialogUpdateRange(curSel->rangeEnt));
    }
}

void BookmarkView::onTreeBeginDrag( wxTreeEvent& event ) {

    TreeViewItem* tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(event.GetItem()));
    
    dragItem = nullptr;
    dragItemId = nullptr;

    SetCursor(wxCURSOR_CROSS);

    if (!tvi) {
        event.Veto();
        return;
    }
    
    bool bAllow = false;
    std::wstring dragItemName;
    
    if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
        bAllow = true;
        dragItemName = BookmarkMgr::getActiveDisplayName(tvi->demod);
    } else if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT || tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        bAllow = true;
        dragItemName = BookmarkMgr::getBookmarkEntryDisplayName(tvi->bookmarkEnt);
    }
    
    if (bAllow) {
        wxColour bgColor(ThemeMgr::mgr.currentTheme->generalBackground);
        wxColour textColor(ThemeMgr::mgr.currentTheme->text);
        
        m_treeView->SetBackgroundColour(textColor);
        m_treeView->SetForegroundColour(bgColor);
//        m_treeView->SetToolTip("Dragging " + dragItemName);
        
        dragItem = tvi;
        dragItemId = event.GetItem();

        visualDragItem = new BookmarkViewVisualDragItem(dragItemName);
        
        event.Allow();
    } else {
        event.Veto();
    }
}


void BookmarkView::onTreeEndDrag( wxTreeEvent& event ) {

    wxColour bgColor(ThemeMgr::mgr.currentTheme->generalBackground);
    wxColour textColor(ThemeMgr::mgr.currentTheme->text);
    
    m_treeView->SetBackgroundColour(bgColor);
    m_treeView->SetForegroundColour(textColor);
    m_treeView->UnsetToolTip();

    SetCursor(wxCURSOR_ARROW);

    if (visualDragItem != nullptr) {
        visualDragItem->Destroy();
        delete visualDragItem;
        visualDragItem = nullptr;
    }
    
    if (!event.GetItem()) {
        event.Veto();
        return;
    }

    TreeViewItem* tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(event.GetItem()));

    if (!tvi) {
        if (event.GetItem() == bookmarkBranch) {
            if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) {
                doBookmarkActive(BOOKMARK_VIEW_STR_UNNAMED, dragItem->demod);
            } else if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) {
                doBookmarkRecent(BOOKMARK_VIEW_STR_UNNAMED, dragItem->bookmarkEnt);
            } else if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
                doMoveBookmark(dragItem->bookmarkEnt, BOOKMARK_VIEW_STR_UNNAMED);
            }
        }
        return;
    }
    
    if (tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_GROUP || tvi->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) {
        if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_ACTIVE) { // Active -> Group Item
            doBookmarkActive(tvi->groupName, dragItem->demod);
        } else if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_RECENT) { // Recent -> Group Item
            doBookmarkRecent(tvi->groupName, dragItem->bookmarkEnt);
            m_treeView->Delete(dragItemId);
        } else if (dragItem && dragItem->type == TreeViewItem::Type::TREEVIEW_ITEM_TYPE_BOOKMARK) { // Bookmark -> Group Item
            doMoveBookmark(dragItem->bookmarkEnt, tvi->groupName);
        }
    }
}


void BookmarkView::onTreeItemGetTooltip( wxTreeEvent& event ) {

    event.Skip();
}


void BookmarkView::onEnterWindow( wxMouseEvent&  event ) {
    mouseTracker.OnMouseEnterWindow(event);

#ifdef _WIN32
    if (wxGetApp().getAppFrame()->canFocus()) {
        //make mousewheel work in the tree view.
        m_treeView->SetFocus();
    }
#endif

    setStatusText("Drag & Drop to create / move bookmarks, Group and arrange bookmarks, quick Search by keywords.");
}


void BookmarkView::onLeaveWindow( wxMouseEvent& event ) {
    mouseTracker.OnMouseLeftWindow(event);
}

void BookmarkView::onMotion( wxMouseEvent& event ) {
    mouseTracker.OnMouseMoved(event);

    wxPoint pos = ClientToScreen(event.GetPosition());
    
    pos += wxPoint(15,-5);
    
    if (visualDragItem != nullptr) {
        visualDragItem->SetPosition(pos);
    }
   
    event.Skip();
}

void BookmarkView::setStatusText(const std::string& statusText) {
    //make tooltips active on the tree view.
    wxGetApp().getAppFrame()->setStatusText(m_treeView, statusText);
}

TreeViewItem *BookmarkView::itemToTVI(wxTreeItemId item) {
    TreeViewItem* tvi = nullptr;
    
    if (item != nullptr) {
        tvi = dynamic_cast<TreeViewItem*>(m_treeView->GetItemData(item));
    }
    
    return tvi;
}

void BookmarkView::onSearchTextFocus( wxMouseEvent&  event ) {

    mouseTracker.OnMouseMoved(event);
    
    //apparently needed ???
    m_searchText->SetFocus();

    if (m_searchText->GetValue() == L"Search..") {
        //select the whole field, so that typing 
        //replaces the whole text by the new one right away.  
        m_searchText->SetSelection(-1, -1);
    }
    else if (!m_searchText->GetValue().Trim().empty()) {
        //position at the end of the existing field, so we can append 
        //or truncate the existing field.
        m_searchText->SetInsertionPointEnd();
    }
    else {
        //empty field, restore displaying L"Search.."
        m_searchText->SetValue(L"Search..");
        m_searchText->SetSelection(-1, -1);
    }
}


void BookmarkView::onSearchText( wxCommandEvent& /* event */ ) {

    std::wstring searchText = m_searchText->GetValue().Trim().Lower().ToStdWstring();
    
   searchKeywords.clear();
    
   if (searchText.length() != 0) {
        std::wstringstream searchTextLo(searchText);
        std::wstring tmp;
        
        while(std::getline(searchTextLo, tmp, L' ')) {
            if (tmp.length() != 0 && tmp.find(L"search.") == std::wstring::npos) {
                searchKeywords.push_back(tmp);
//                std::wcout << L"Keyword: " << tmp << '\n';
            }
        }
    }
    
    if (!searchKeywords.empty() && !m_clearSearchButton->IsShown()) {
        m_clearSearchButton->Show();
        refreshLayout();
    } else if (searchKeywords.empty() && m_clearSearchButton->IsShown()) {
        m_clearSearchButton->Hide();
        refreshLayout();
    }
    
    wxGetApp().getBookmarkMgr().updateActiveList();
    wxGetApp().getBookmarkMgr().updateBookmarks();
}


void BookmarkView::onClearSearch( wxCommandEvent& /* event */ ) {

    m_clearSearchButton->Hide();
    m_searchText->SetValue(L"Search..");
    m_treeView->SetFocus();

    searchKeywords.clear();

    wxGetApp().getBookmarkMgr().updateActiveList();
    wxGetApp().getBookmarkMgr().updateBookmarks();
    refreshLayout();
}

BookmarkRangeEntryPtr BookmarkView::makeActiveRangeEntry() {
    BookmarkRangeEntryPtr re(new BookmarkRangeEntry);
   
    re->startFreq = wxGetApp().getAppFrame()->getViewCenterFreq() - (wxGetApp().getAppFrame()->getViewBandwidth()/2);
    re->endFreq = wxGetApp().getAppFrame()->getViewCenterFreq() + (wxGetApp().getAppFrame()->getViewBandwidth()/2);
    
	//to prevent problems, always make the re->freq the middle of the interval.
	re->freq = (re->startFreq + re->endFreq) / 2;

    return re;
}


void BookmarkView::SetTreeItemData(const wxTreeItemId& item, wxTreeItemData *data) {

    TreeViewItem *itemData = itemToTVI(item);
    // cleanup previous data, if any
    delete itemData;

    m_treeView->SetItemData(item, data);
}
