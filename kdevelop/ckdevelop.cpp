/***************************************************************************
                    ckdevelop.cpp - the main class in CKDevelop
                             -------------------                                         

    begin                : 20 Jul 1998                                        
    copyright            : (C) 1998 by Sandy Meier                         
    email                : smeier@rz.uni-potsdam.de                                     
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   * 
 *                                                                         *
 ***************************************************************************/

#include "ckdevelop.h"


#include "cclassview.h"
#include "ccreatedocdatabasedlg.h"
#include "cdocbrowser.h"
#include "ceditwidget.h"
#include "cerrormessageparser.h"
#include "cexecuteargdlg.h"
#include "cfinddoctextdlg.h"
#include "ckdevaccel.h"
#include "ckdevsetupdlg.h"
#include "clogfileview.h"
#include "coutputwidget.h"
#include "crealfileview.h"
#include "ctabctl.h"
#include "ctoolclass.h"
#include "ctoolsconfigdlg.h"
#include "cupdatekdedocdlg.h"

#include "dbgtoolbar.h"
#include "dbgpsdlg.h"
#include "debug.h"
#include "doctreeview.h"
#include "grepdialog.h"
#include "structdef.h"

#include "print/cprintdlg.h"
#include "vc/versioncontrol.h"
#include "./dbg/vartree.h"
#include "./dbg/gdbcontroller.h"
#include "./dbg/brkptmanager.h"
#include "./dbg/breakpoint.h"
#include "./dbg/framestack.h"
#include "./dbg/memview.h"
#include "./dbg/disassemble.h"
#include "./kwrite/kwdoc.h"
#include "ktipofday.h"


#include <kaboutdialog.h>
#include <kcombobox.h>
#include <kcursor.h>
#include <kfiledialog.h>
#include <khtmlview.h>
#include <kkeydialog.h>
#include <klocale.h>
#include <kmenubar.h>
#include <kmessagebox.h>
#include <krun.h>
#include <kstddirs.h>
#include <ktabctl.h>
#include <qprogressbar.h>

#include <qclipbrd.h>
#include <qdir.h>
#include <qevent.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qfont.h>
#include <qregexp.h>
#include <qtextstream.h>
#include <qtoolbar.h>
#include <qmessagebox.h>
#include <qwhatsthis.h>

#include <iostream.h>
#include <stdlib.h>
#include <ctype.h>

#include <X11/Xlib.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////
// FILE-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

void CKDevelop::slotFileNew(){
  
  slotStatusMsg(i18n("Creating new file..."));
  newFile(false);
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotFileNew(const char* dir){

  slotStatusMsg(i18n("Creating new file..."));
  newFile(false, dir);
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotFileOpen(){
  slotStatusMsg(i18n("Opening file..."));

  QString str;

  //modif by Benoit Cerrina 15 Dec 99
  if(!lastOpenDir.isEmpty())
  {
    str = KFileDialog::getOpenFileName(lastOpenDir,"*");
  }
  else if(project){
    str = KFileDialog::getOpenFileName(prj->getProjectDir(),"*");
  }
  else{
    str = KFileDialog::getOpenFileName(QString::null,"*");
  }  
  if (!str.isEmpty())
  {
    int lSlashPos = str.findRev('/');
    if (lSlashPos != -1)
    {
      lastOpenDir = str;
      lastOpenDir.truncate(lSlashPos);
    }
  }
  //end modif

  if (!str.isEmpty()) // nocancel
  {
    switchToFile(str);
  }

  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotFileOpen( int id_ )
{
  slotStatusMsg(i18n("Opening file..."));

  int index;
  if ((index = file_open_popup->indexOf(id_)) >=0)
  {
    QString str=file_open_list.at(index);
    switchToFile(str);
  }

  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotFileClose()
{
  slotStatusMsg(i18n("Closing file..."));
  QString filename = edit_widget->getName();
  int message_result;

  if(edit_widget->isModified())
  {
    // no autosave if the user intends to save a file
    if (bAutosave)
      saveTimer->stop();
  
    message_result = KMessageBox::warningYesNoCancel(this,
                        i18n("The document was modified,save?"),
                        i18n("Save?"));
    // restart autosaving
    if (bAutosave)
      saveTimer->start(saveTimeout);
  
    if (message_result == 1)
    { // yes
      if (isUntitled(filename))
      {
        if (!fileSaveAs())
          message_result=3;    // simulate here cancel because fileSaveAs failed....
      }
      else
      {
        saveFileFromTheCurrentEditWidget();
        if (edit_widget->isModified())
          message_result=3;       // simulate cancel because doSave went wrong!
      }
    }
  
    if (message_result == 3) // cancel
    {
      setInfoModified(filename, edit_widget->isModified());
      slotStatusMsg(i18n("Ready."));
      return;
    }
  }

  removeFileFromEditlist(filename);
  setMainCaption();
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotFileCloseAll()
{
  slotStatusMsg(i18n("Closing all files..."));
  TEditInfo* actual_info;
  QStrList handledNames;
  bool cont=true;

  setInfoModified(header_widget->getName(), header_widget->isModified());
  setInfoModified(cpp_widget->getName(), cpp_widget->isModified());

  for(actual_info=edit_infos.first();cont && actual_info != 0;)
  {
    TEditInfo *next_info=edit_infos.next();
    if(actual_info->modified && handledNames.contains(actual_info->filename)<1)
    {
      QString prjName("");
      if (prj)
        prjName = "\n" + prj->getProjectName() + "\n\n";

#warning FIXME MessageBox needed with an extra button.
//      KMessageBox *files_close=
//        new KMessageBox(this,
//                        i18n("The project\n")+prjName
//                          +i18n("contains changed files. Save modified file\n\n")
//                          +actual_info->filename+" ?\n\n",
//                          i18n("Save changed files ?"),
//                          i18n("Yes"), i18n("No"), /*i18n("Save all"), */i18n("Cancel"));
//
//      // show the messagea and store result in result:
//
//      files_close->show();
//
//      int result=files_close->result();
      int result = KMessageBox::warningYesNoCancel(this,
                          i18n("The project\n")+prjName
                          +i18n("contains changed files. Save modified file\n\n")
                          +actual_info->filename+" ?\n\n",
                          i18n("Save changed files ?"));

      // create the save project messagebox

      // what to do
      if(result==1) // Yes- only save the actual file
      {
        // save file as if Untitled and close file
        if(isUntitled(actual_info->filename))
        {
          switchToFile(actual_info->filename);
          handledNames.append(actual_info->filename);
          cont=fileSaveAs();
          next_info=edit_infos.first(); // start again... 'cause we deleted an entry
        }        
        else // Save file and close it
        {
          switchToFile(actual_info->filename);
          handledNames.append(actual_info->filename);
          slotFileSave();
          actual_info->modified=edit_widget->isModified();
          cont=!actual_info->modified; //something went wrong
        }
      }

      if(result==2) // No - no save but close
      {
        handledNames.append(actual_info->filename);
        actual_info->modified=false;
        removeFileFromEditlist(actual_info->filename); // immediate remove
        next_info=edit_infos.first(); // start again... 'cause we deleted an entry
      }

      if(result==3) // Save all
      {
        slotFileSaveAll();
        break;
      }

      if(result==4) // Cancel
      {
        cont=false;
        break;
      }  
    }  // end actual file close

    actual_info=next_info;
  } // end for-loop

  // check if something went wrong with saving
  if ( cont )
  {
    for( actual_info=edit_infos.first();
         cont && actual_info != 0;
         actual_info=edit_infos.next())
    {
      if ( actual_info->modified )
        cont=false;
    } // end for-loop

    if(cont)
    {
      header_widget->clear();
      cpp_widget->clear();
      menu_buffers->clear();

      //clear all edit_infos before starting a new project
      edit_infos.clear();

      header_widget->setName(i18n("Untitled.h"));
      cpp_widget->setName(i18n("Untitled.cpp"));
      TEditInfo* edit1 = new TEditInfo;
      TEditInfo* edit2 = new TEditInfo;
      edit1->filename = header_widget->getName();
      edit2->filename = cpp_widget->getName();

      edit1->id = menu_buffers->insertItem(edit1->filename,-2,0);
      edit1->modified=false;
      edit2->id = menu_buffers->insertItem(edit2->filename,-2,0);
      edit2->modified=false;
      edit_infos.append(edit1);
      edit_infos.append(edit2);
    }
  }

  slotStatusMsg(i18n("Ready."));
}

bool CKDevelop::saveFileFromTheCurrentEditWidget(){
  QString filename=edit_widget->getName();
  TEditInfo* actual_info;
  QFileInfo file_info(filename);
  
  for(actual_info=edit_infos.first();actual_info != 0;actual_info=edit_infos.next())
  {
    if (actual_info->filename == filename ){
      break;
    }
  }

  if (actual_info == 0)
    return false; //oops :-(

  if(file_info.lastModified() != actual_info->last_modified)
  {
    if (KMessageBox::No == KMessageBox::questionYesNo(this,

                    i18n("The file ") + filename + i18n(" was modified outside\n this editor.Save anyway?"),
                    i18n("File modified")))
      return false;
  }
  edit_widget->doSave();
  QFileInfo file_info2(filename);
  actual_info->last_modified = file_info2.lastModified();
  return true;
}

void CKDevelop::slotFileSave(){

  QString filename=edit_widget->getName();
  QString sShownFilename=QFileInfo(filename).fileName();
  slotStatusMsg(i18n("Saving file ")+sShownFilename);
  
 

  if(isUntitled(filename)){
    slotFileSaveAs();
  }
  else
  {
    saveFileFromTheCurrentEditWidget(); // save the current file
    setInfoModified(filename, edit_widget->isModified());
    QStrList lSavedFile;
    lSavedFile.append(filename);
#ifdef WITH_CPP_REPARSE
    if (project)
#else
      if (project && edit_widget==header_widget)
#endif
        refreshClassViewByFileList(&lSavedFile);
  }

  slotStatusMsg(i18n("Ready."));
  QString sHelpMsg=i18n("File ");
  sHelpMsg+=sShownFilename;
  if (edit_widget->isModified())
    sHelpMsg+=i18n(" not saved.");
  else
      sHelpMsg+=i18n(" saved.");

  slotStatusHelpMsg(sHelpMsg);
}

void CKDevelop::slotFileSaveAs(){
    slotStatusMsg(i18n("Save file as..."));
    
    fileSaveAs();

    setMainCaption();
    slotStatusMsg(i18n("Ready."));
}

#include <iostream.h>
void CKDevelop::slotFileSaveAll()
{
    QStrList handledNames;
    TEditInfo* actual_info, *cpp_info, *header_info;
    CEditWidget blind_widget;
    QStrList iFileList(false);
    bool mod=false;

    int visibleTab=s_tab_view->getCurrentTab();

    // ok,its a dirty implementation  :-)
    if(!bAutosave || !saveTimer->isActive())
      slotStatusMsg(i18n("Saving all changed files..."));
    else
      slotStatusMsg(i18n("Autosaving..."));

//    mainSplitter->setUpdatesEnabled(false);

    setInfoModified(header_widget->getName(), header_widget->isModified());
    setInfoModified(cpp_widget->getName(), cpp_widget->isModified());

    header_info=getInfoFromFilename(header_widget->getName());
    if (header_info)
      header_info->text=header_widget->text();
    cpp_info=getInfoFromFilename(cpp_widget->getName());
    if (cpp_info)
      cpp_info->text=cpp_widget->text();

    statProg->setTotalSteps(edit_infos.count());
    statProg->show();

    for(actual_info=edit_infos.first();actual_info != 0;)
    {
      int i=0;
      TEditInfo *next_info=edit_infos.next();
      statProg->setProgress(++i);

//      cerr << "checking: " << actual_info->filename << "\n";
//      cerr << " " << ((actual_info->modified) ? "modified" : "not modified") << "\n";

      if(!isUntitled(actual_info->filename) && actual_info->modified &&
        handledNames.contains(actual_info->filename)<1)
      {
        int qYesNo=KMessageBox::Yes;
        handledNames.append(actual_info->filename);
        QFileInfo file_info(actual_info->filename);
        if (file_info.lastModified() != actual_info->last_modified)
        {
          qYesNo = KMessageBox::questionYesNo(this,
                                i18n("The file %1 was modified outside\nthis editor. Save anyway?").arg(actual_info->filename),
                                i18n("File modified"));
        }

        if (qYesNo==KMessageBox::Yes)
        {
          QFileInfo file_info(actual_info->filename);
          bool isModified;
          blind_widget.setName(actual_info->filename);
          blind_widget.setText(actual_info->text);
          blind_widget.toggleModified(true);
          blind_widget.doSave();
          isModified=blind_widget.isModified();
//
//      cerr << "doing save " << ((!isModified) ? "success" : "failed") << "\n";
//

          if (actual_info==cpp_info)
             cpp_widget->setModified(isModified);
          if (actual_info==header_info)
             header_widget->setModified(isModified);

          actual_info->modified = isModified;
          if (!isModified)
          {
#ifdef WITH_CPP_REPARSE
              mod=true;
#else
              mod|=(actual_info->filename.right(2)==".h" || actual_info->filename.right(4)==".hxx");
#endif
              iFileList.append(actual_info->filename);
              actual_info->last_modified = file_info.lastModified();
        }
      }
    }
    actual_info=next_info;
  }

//  statProg->hide();
  statProg->reset();

  if (project && !iFileList.isEmpty() && mod)
    refreshClassViewByFileList(&iFileList);

  setMainCaption(visibleTab);
  if (visibleTab == CPP || visibleTab == HEADER)
  {
    edit_widget->setFocus();
  }
  if (visibleTab == BROWSER)
  {
    browser_widget->view()->setFocus();
  }
//  mainSplitter->setUpdatesEnabled(true);
  slotStatusMsg(i18n("Ready."));
}


void CKDevelop::slotFilePrint(){
  QString file;
  slotFileSave();
  file = edit_widget->getName();
  CPrintDlg* printerdlg = new CPrintDlg(this, file, "suzus");
  printerdlg->resize(600,480);
  printerdlg->exec();
  delete (printerdlg);
}

void CKDevelop::slotFileQuit(){
  slotStatusMsg(i18n("Exiting..."));
  saveOptions();
  close();
}

///////////////////////////////////////////////////////////////////////////////////////
// EDIT-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

void CKDevelop::slotEditUndo(){
  edit_widget->undo();
}
void CKDevelop::slotEditRedo(){
  edit_widget->redo();
}
void CKDevelop::slotEditCut(){
  slotStatusMsg(i18n("Cutting..."));
  edit_widget->cut();
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditCopy(){
  slotStatusMsg(i18n("Copying..."));
  if(s_tab_view->getCurrentTab()==BROWSER){
    browser_widget->slotCopyText();
  }
  else
    edit_widget->copyText();
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditPaste(){
  slotStatusMsg(i18n("Pasting selection..."));
  edit_widget->paste();
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditInsertFile(){
  slotStatusMsg(i18n("Inserting file contents..."));
  edit_widget->insertFile();
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditSearch(){
  
  slotStatusMsg(i18n("Searching..."));
  if(s_tab_view->getCurrentTab()==BROWSER){
      CFindDocTextDlg* help_srch_dlg=new CFindDocTextDlg(this,"Search_for_Help_on");
      connect(help_srch_dlg,SIGNAL(signalFind(QString)),browser_widget,SLOT(slotFindTextNext(QString)));
      help_srch_dlg->exec();
      delete help_srch_dlg;
  }
  else {
      edit_widget->search();
  }
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditRepeatSearch(int back){
  slotStatusMsg(i18n("Repeating last search..."));
  if(s_tab_view->getCurrentTab()==BROWSER){
    browser_widget->findTextNext(QRegExp(doc_search_text),true);
  }
  else{
    edit_widget->searchAgain(back==1);
  }
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditRepeatSearchBack(){
  slotEditRepeatSearch(1);	// flag backward search
}
void CKDevelop::slotEditSearchInFiles(){
  slotStatusMsg(i18n("Searching in Files..."));
  if(project){
    grep_dlg->setDirName(prj->getProjectDir());
  }
  grep_dlg->show();
  grep_dlg->raise();
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotEditSearchInFiles(QString search){
  int pos;
  slotStatusMsg(i18n("Searching in Files..."));

  if(project){
    grep_dlg->setDirName(prj->getProjectDir());
  }
  grep_dlg->show();

  search.replace(QRegExp("^\n"), "");
  pos=search.find("\n");
  if (pos>-1)
   search=search.left(pos);

  search=realSearchText2regExp(search, true);
  grep_dlg->slotSearchFor(search);
  grep_dlg->raise();
  slotStatusMsg(i18n("Ready."));
  }

void CKDevelop::slotEditSearchText(){
  QString text;
  if(s_tab_view->getCurrentTab()==BROWSER){
    text = browser_widget->selectedText();
  }
  else{
    text = edit_widget->markedText();
    if(text == ""){
      text = edit_widget->currentWord();
    }
  }

  if (!text.isEmpty())
    slotEditSearchInFiles(text);
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotEditReplace(){
  slotStatusMsg(i18n("Replacing..."));
  edit_widget->replace();
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotEditIndent(){
  edit_widget->indent();
}
void CKDevelop::slotEditUnindent(){
  edit_widget->unIndent();
}

void CKDevelop::slotEditComment(){
  edit_widget->comment();
}
void CKDevelop::slotEditUncomment(){
  edit_widget->unComment();
}

/*
void CKDevelop::slotEditSpellcheck(){
  edit_widget->spellcheck();
}
*/

void CKDevelop::slotEditSelectAll(){
  slotStatusMsg(i18n("Selecting all..."));
  edit_widget->selectAll();
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotEditInvertSelection(){
    edit_widget->invertSelection();
}
void CKDevelop::slotEditDeselectAll(){
    edit_widget->deselectAll();
}

///////////////////////////////////////////////////////////////////////////////////////
// VIEW-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

void CKDevelop::slotViewGotoLine(){
  slotStatusMsg(i18n("Switching to selected line..."));
  edit_widget->gotoLine();
  slotStatusMsg(i18n("Ready."));
}
/** jump to the next error, based on the make output*/
void CKDevelop::slotViewNextError(){
  TErrorMessageInfo info = error_parser->getNext();
  if(info.filename != ""){
    messages_widget->setCursorPosition(info.makeoutputline-1,0);
    switchToFile(info.filename,info.errorline-1);
//    if(!bKDevelop){
//      switchToKDevelop();
//    }
    slotStatusMsg(messages_widget->textLine(info.makeoutputline-1));
  }
  else{
    XBell(kapp->getDisplay(),100); // not a next found, beep
  }

  //enable/disable the menus/toolbars
  if(error_parser->hasNext()){
    enableCommand(ID_VIEW_NEXT_ERROR);
  }
  else{
    disableCommand(ID_VIEW_NEXT_ERROR);
  }
  
  if(error_parser->hasPrev()){
    enableCommand(ID_VIEW_PREVIOUS_ERROR);
  }
  else{
    disableCommand(ID_VIEW_PREVIOUS_ERROR);
  }
}
/** jump to the previews error, based on the make output*/
void CKDevelop::slotViewPreviousError(){
  TErrorMessageInfo info = error_parser->getPrev();
  if(info.filename != ""){
    messages_widget->setCursorPosition(info.makeoutputline-1,0);
    switchToFile(info.filename,info.errorline-1);
//    if(!bKDevelop){
//      switchToKDevelop();
//    }
    slotStatusMsg(messages_widget->textLine(info.makeoutputline-1));
  }
  else{
    XBell(kapp->getDisplay(),100); // not a previous found, beep
  }
  //enable/disable the menus/toolbars
  if(error_parser->hasNext()){
    enableCommand(ID_VIEW_NEXT_ERROR);
  }
  else{
    disableCommand(ID_VIEW_NEXT_ERROR);
  }
  
  if(error_parser->hasPrev()){
    enableCommand(ID_VIEW_PREVIOUS_ERROR);
  }
  else{
    disableCommand(ID_VIEW_PREVIOUS_ERROR);
  }
}

void CKDevelop::slotViewTTreeView(){
  if(treedock->isVisible()){
    view_menu->setItemChecked(ID_VIEW_TREEVIEW,false);
    toolBar()->setButton(ID_VIEW_TREEVIEW,false);
  }
  else{
    view_menu->setItemChecked(ID_VIEW_TREEVIEW,true);
    toolBar()->setButton(ID_VIEW_TREEVIEW,true);
  }
  treedock->changeHideShowState();
}

void CKDevelop::slotViewTOutputView(){
  if(outputdock->isVisible()){
    view_menu->setItemChecked(ID_VIEW_OUTPUTVIEW,false);
    toolBar()->setButton(ID_VIEW_OUTPUTVIEW,false);
  }
  else{
    view_menu->setItemChecked(ID_VIEW_OUTPUTVIEW,true);
    toolBar()->setButton(ID_VIEW_OUTPUTVIEW,true);
  }
  outputdock->changeHideShowState();
}


void CKDevelop::slotViewTStdToolbar(){
 if(view_menu->isItemChecked(ID_VIEW_TOOLBAR)){
   view_menu->setItemChecked(ID_VIEW_TOOLBAR,false);
    toolBar()->hide();
  }
  else{
    view_menu->setItemChecked(ID_VIEW_TOOLBAR,true);
    toolBar()->show();
  }

}
void CKDevelop::slotViewTBrowserToolbar(){
  if(view_menu->isItemChecked(ID_VIEW_BROWSER_TOOLBAR)){
    view_menu->setItemChecked(ID_VIEW_BROWSER_TOOLBAR,false);
    toolBar(ID_BROWSER_TOOLBAR)->hide();
  }
  else{
    view_menu->setItemChecked(ID_VIEW_BROWSER_TOOLBAR,true);
    toolBar(ID_BROWSER_TOOLBAR)->show();
  }
}

void CKDevelop::slotViewTStatusbar(){
  if(view_menu->isItemChecked(ID_VIEW_STATUSBAR)){
    view_menu->setItemChecked(ID_VIEW_STATUSBAR,false);
    statusBar()->hide();
  }
  else{
    view_menu->setItemChecked(ID_VIEW_STATUSBAR,true);
    statusBar()->show();
  }
}

void CKDevelop::slotViewRefresh(){
  refreshTrees();
}

void CKDevelop::slotViewTabIcons(){
    view_tab_menu->setItemChecked(ID_VIEW_TAB_ICONS,true);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT,false);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT_ICONS,false);
    t_tab_view->changeTab(class_tree,SmallIcon("CVclass"),"");
    t_tab_view->changeTab(log_file_tree,SmallIcon("attach"),"");
    t_tab_view->changeTab(real_file_tree,SmallIcon("folder"),"");
    t_tab_view->changeTab(doc_tree,SmallIcon("contents"),"");
    if(var_viewer)
      t_tab_view->changeTab(var_viewer,SmallIcon("brace"),"");

}
void CKDevelop::slotViewTabText(){
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT,true);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_ICONS,false);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT_ICONS,false);
    t_tab_view->removePage(class_tree);
    t_tab_view->removePage(log_file_tree);
    t_tab_view->removePage(real_file_tree);
    t_tab_view->removePage(doc_tree);
    if(var_viewer)
      t_tab_view->removePage(var_viewer);
    t_tab_view->addTab(class_tree,i18n("C&lasses"));
    t_tab_view->addTab(log_file_tree,i18n("G&roups"));
    t_tab_view->addTab(real_file_tree,i18n("File&s"));
    t_tab_view->addTab(doc_tree,i18n("Boo&ks"));
    if(var_viewer)
      t_tab_view->addTab(var_viewer,i18n("W&atch"));

}
void CKDevelop::slotViewTabTextIcons(){
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT_ICONS,true);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_ICONS,false);
    view_tab_menu->setItemChecked(ID_VIEW_TAB_TEXT,false);

    t_tab_view->changeTab(class_tree,SmallIcon("CVclass"),i18n("C&lasses"));
    t_tab_view->changeTab(log_file_tree,SmallIcon("attach"),i18n("G&roups"));
    t_tab_view->changeTab(real_file_tree,SmallIcon("folder"),i18n("File&s"));
    t_tab_view->changeTab(doc_tree,SmallIcon("contents"),i18n("Boo&ks"));
    if(var_viewer)
      t_tab_view->changeTab(var_viewer,SmallIcon("brace"),i18n("W&atch"));
}



///////////////////////////////////////////////////////////////////////////////////////
// BUILD-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

void CKDevelop::slotBuildCompileFile(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);
  slotFileSave();
  setToolMenuProcess(false);
  slotStatusMsg(i18n("Compiling ")+cpp_widget->getName());
  messages_widget->clear();
  process.clearArguments();
  // get the filename of the implementation file to compile and change extension for make
  //KDEBUG1(KDEBUG_INFO,CKDEVELOP,"ObjectFile= %s",QString(fileinfo.baseName()+".o").data());
//  cerr << "ObjectFile= " << fileinfo.baseName()+".o";
  QFileInfo fileinfo(cpp_widget->getName());
  QString actualDir=fileinfo.dirPath();
  QDir::setCurrent(actualDir);
  error_parser->setStartDir(actualDir);

  if (prj->getProjectType()!="normal_empty")
  {
   QString flaglabel=(prj->getProjectType()=="normal_c") ? "CFLAGS=\"" : "CXXFLAGS=\"";
   process << flaglabel;
   if (!prj->getCXXFLAGS().isEmpty() || !prj->getAdditCXXFLAGS().isEmpty())
   {
     if (!prj->getCXXFLAGS().isEmpty())
     {
       process << prj->getCXXFLAGS() << " ";
     }
     if (!prj->getAdditCXXFLAGS().isEmpty())
     {
       process << prj->getAdditCXXFLAGS();
     }
   }
   process  << "\" " << "LDFLAGS=\" " ;
   if (!prj->getLDFLAGS().isEmpty())
   {
     process << prj->getLDFLAGS();
   }
   process  << "\" ";
   process << make_cmd << fileinfo.baseName()+".o";
  }
  else
  {
    QString makefile=actualDir+"/Makefile";
    process << make_cmd;
    if (!QFileInfo(makefile).exists())
    {
      makefile=prj->getProjectDir()+prj->getSubDir()+"Makefile";
      if (!QFileInfo(makefile).exists())
        makefile=prj->getProjectDir()+"Makefile";
      if (QFileInfo(makefile).exists())
        process << "-f" << makefile;
    }
    process << fileinfo.baseName()+".o";
  }


  process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
}

void CKDevelop::slotBuildRun()
{
  KConfig *config=KGlobal::config();
  bool isDirty=isProjectDirty();
  int qYesNoCancel=0;
  int rebuildType;

  config->setGroup("MakeOptionsSettings");
  rebuildType=config->readNumEntry("RebuildType", 0);
  if (rebuildType==0 && isDirty)
    qYesNoCancel=QMessageBox::warning(this,i18n("Project sources have been modified"),
                    i18n("Should the project be rebuild before starting the application?"),
                    i18n("Yes"), i18n("No"), i18n("Cancel"),0,2);

  if (qYesNoCancel!=2)
  {
    beep=false;
    prj->writeProject();
    if (rebuildType==2 || (isDirty && qYesNoCancel==0))
    {
      next_job = "run";
      slotBuildMake();
    }
    else
      slotStartRun();
  }
}

void CKDevelop::slotBuildRunWithArgs()
{
  KConfig *config=KGlobal::config();
  bool isDirty=isProjectDirty();
  int qYesNoCancel=0;
  int rebuildType;

  config->setGroup("MakeOptionsSettings");
  rebuildType=config->readNumEntry("RebuildType", 0);
  if (rebuildType==0 && isDirty)
    qYesNoCancel=QMessageBox::warning(this,i18n("Project sources have been modified"),
                    i18n("Should the project be rebuild before starting the application?"),
                    i18n("Yes"), i18n("No"), i18n("Cancel"),0,2);

  if (qYesNoCancel!=2)
  {
    beep=false;
    prj->writeProject();
    if (rebuildType==2 || (isDirty && qYesNoCancel==0))
    {
      next_job = "run_with_args";
      slotBuildMake();
    }
    else
      slotStartRun(true);
  }
}

void CKDevelop::slotStartRun(bool bWithArgs)
{
  bool bContinue=true;
   // rest from the buildRun
  appl_process.clearArguments();

  QString runFromDir = prj->getRunFromDir();
  QDir::setCurrent(runFromDir);

  QString libtool     = prj->getLibtool();
  QString binProgram  = prj->getExecutable();

  stdin_stdout_widget->clear();
  stderr_widget->clear();

  QString args = prj->getExecuteArgs();

  if(bWithArgs)
  {
    CExecuteArgDlg argdlg(this,i18n("Arguments"),i18n("Execute with Arguments"),args);
    if(argdlg.exec())
    {
        args=argdlg.getArguments();
        prj->setExecuteArgs(args);
        if(!args.isEmpty())
        {
          binProgram = binProgram+" "+args;
        }
    }
    else
      bContinue=false;
  }

  if (bContinue)
  {
    slotStatusMsg(i18n("Running %1 (from %2)").arg(prj->getBinPROGRAM()).arg(runFromDir));
    // Warning: not every user has the current directory in his path !
    if(prj->getProjectType() == "normal_cpp" || prj->getProjectType() == "normal_c")
    {
       o_tab_view->setCurrentTab(STDINSTDOUT);
       QString term = "xterm";
       QString exec_str = term + " -e sh -c '" + binProgram + "'";

       if(CToolClass::searchInstProgram("konsole"))
       {
         term = "konsole";
       }
       if(CToolClass::searchInstProgram("ksh"))
       {
         exec_str = term + " -e ksh -c '" + binProgram +
            ";echo \"\n" + QString(i18n("Press Enter to continue!")) + "\";read'";
       }
       if(CToolClass::searchInstProgram("csh"))
       {
         exec_str = term +" -e csh -c '" + binProgram +
            ";echo \"\n" + QString(i18n("Press Enter to continue!")) + "\";$<'";
       }
       if(CToolClass::searchInstProgram("tcsh"))
       {
          exec_str =  term +" -e tcsh -c '" + binProgram +
            ";echo \"\n" + QString(i18n("Press Enter to continue!")) + "\";$<'";
       }
       if(CToolClass::searchInstProgram("bash"))
       {
          exec_str =  term +" -e bash -c '" + binProgram +
          ";echo \"\n" + QString(i18n("Press Enter to continue!")) + "\";read'";
       }
       appl_process << exec_str;
       cerr << endl << "EXEC:" << exec_str;
    }
    else if (prj->isKDE2Project()) {
       const QString oldGroup = config->group();
       config->setGroup("QT2");
       QString kde2dir =  QString("KDEDIRS=") + config->readEntry("kde2dir") + " ";
       config->setGroup(oldGroup);

       appl_process << kde2dir << binProgram;
       cerr << endl << "EXEC:" << kde2dir << binProgram;
       o_tab_view->setCurrentTab(STDERR);
    }
    else if(prj->isKDEProject() || prj->isQtProject() || prj->isQt2Project())
    {
      appl_process << binProgram;
      cerr << endl << "EXEC:" << binProgram;
      o_tab_view->setCurrentTab(STDERR);
    }
    else
    {
      appl_process << binProgram;
      cerr << endl << "EXEC:" << binProgram;
      o_tab_view->setCurrentTab(STDERR);
    }

    setToolMenuProcess(false);
    appl_process.start(KProcess::NotifyOnExit,KProcess::All);
  }
}

void CKDevelop::slotDebugActivator(int id)
{
  switch (id)
  {
    case ID_DEBUG_START:
      slotBuildDebug();
      break;
    case ID_DEBUG_RUN:
      ASSERT(dbgInternal);
      slotDebugRun();
      break;
    case ID_DEBUG_RUN_CURSOR:
      ASSERT(dbgInternal);
      edit_widget->slotRunToCursor();
      break;
    case ID_DEBUG_STEP:
      ASSERT(dbgInternal && dbgController);
      dbgController->slotStepInto();
      break;
    case ID_DEBUG_STEP_INST:
      ASSERT(dbgInternal && dbgController);
      dbgController->slotStepIntoIns();
      break;
    case ID_DEBUG_NEXT:
      ASSERT(dbgInternal && dbgController);
      dbgController->slotStepOver();
      break;
    case ID_DEBUG_NEXT_INST:
      ASSERT(dbgInternal && dbgController);
      dbgController->slotStepOverIns();
      break;
    case ID_DEBUG_STOP:
      ASSERT(dbgInternal);
      slotDebugStop();
      break;
    case ID_DEBUG_BREAK_INTO:
      ASSERT(dbgInternal && dbgController);
      dbgController->slotBreakInto();
      break;
    case ID_DEBUG_MEMVIEW:
      ASSERT(dbgInternal);
      slotDebugMemoryView();
      break;
    case ID_DEBUG_FINISH:
      ASSERT(dbgInternal);
      dbgController->slotStepOutOff();
      break;
  }
}

void CKDevelop::slotDebugRunToCursor()
{
  if (!dbgController)
    return;

  edit_widget->slotRunToCursor();
}

void CKDevelop::slotDebugStepInto()
{
  if (!dbgController)
    return;

  dbgController->slotStepInto();
}

void CKDevelop::slotDebugStepIntoIns()
{
  if (!dbgController)
    return;
  dbgController->slotStepIntoIns();
}

void CKDevelop::slotDebugStepOver()
{
  if (!dbgController)
    return;
  dbgController->slotStepOver();
}

void CKDevelop::slotDebugStepOverIns()
{
  if (!dbgController)
    return;

  dbgController->slotStepOverIns();
}

void CKDevelop::slotDebugBreakInto()
{
  if (!dbgController)
    return;
  dbgController->slotBreakInto();
}

void CKDevelop::slotDebugStepOutOff()
{
  if (!dbgController)
    return;

  dbgController->slotStepOutOff();
}

void CKDevelop::slotDebugInterrupt()
{
  if (!dbgController)
    return;

  dbgController->slotBreakInto();
}

void CKDevelop::slotDebugToggleBreakpoint()
{
  if (!brkptManager)
    return;

  edit_widget->slotToggleBreakpoint();
}


void CKDevelop::slotDebugRun()
{
  if (!dbgController)
    return;

  // and start the debugger going
  dbgController->slotRun();
}

void CKDevelop::slotDebugStop()
{
  setDebugMenuProcess(false);

  if (dbgShuttingDown || !dbgInternal)
    return;

  dbgShuttingDown = true;
  delete dbgController;
  dbgController = 0;

  brkptManager->reset();
  frameStack->clear();
  var_viewer->clear();
  disassemble->clear();
  disassemble->slotActivate(false);

#if defined(GDB_MONITOR) || defined(DBG_MONITOR)
//  dbg_widget->clear();
#endif
  edit_widget->clearStepLine();
  brkptManager->refreshBP(edit_widget->getName());

  o_tab_view->setTabEnabled("FStackTab", dbgInternal && dbgController);
  o_tab_view->setTabEnabled("DisassembleTab", dbgInternal && dbgController);
  t_tab_view->setTabEnabled("VARTab", dbgInternal && dbgController);
  frameStack->setEnabled(dbgInternal && dbgController);
  disassemble->setEnabled(dbgInternal && dbgController);
  var_viewer->setEnabled(dbgInternal && dbgController);

  // We disabled autosaving when debugging, so if they wanted
  // it we have to restart it
  if (bAutosave)
    saveTimer->start(saveTimeout);
}

void CKDevelop::slotDebugShowStepInSource(const QString& filename,int linenumber,
                                          const QString& /*address*/)
{
  if (filename.isEmpty())
  {
    edit_widget->clearStepLine();
  }
  else
  {
    // The editor starts at line 0 but GDB starts at line 1. Fix that now!
    switchToFile(filename);
    edit_widget->clearStepLine();
    edit_widget->setStepLine(linenumber-1);
//    machine_widget(address);
  }
}

void CKDevelop::slotDebugGoToSourcePosition(const QString& filename,int linenumber)
{
  switchToFile(filename,linenumber);
}

void CKDevelop::slotDebugRefreshBPState(const Breakpoint* BP)
{
  if (BP->hasSourcePosition() && (edit_widget->getName() == BP->filename()))
  {
    if (BP->isActionDie())
    {
      edit_widget->delBreakpoint(BP->lineNo()-1);
      return;
    }

    // The editor starts at line 0 but GDB starts at line 1. Fix that now!
    edit_widget->setBreakpoint(BP->lineNo()-1, -1/*BP->id()*/, BP->isEnabled(), BP->isPending() );
  }
}

// All we need to do is make sure the display is uptodate.
void CKDevelop::slotDebugBPState(Breakpoint* BP)
{
  slotDebugRefreshBPState(BP);
}


void CKDevelop::slotDebugMemoryView()
{
  if (!dbgController)
    return;

  MemoryView* memoryView = new MemoryView(this, "Memory view");
  connect(  memoryView,     SIGNAL(disassemble(const QString&, const QString&)),
            dbgController,  SLOT(slotDisassemble(const QString&, const QString&)));
  connect(  memoryView,     SIGNAL(memoryDump(const QString&, const QString&)),
            dbgController,  SLOT(slotMemoryDump(const QString&, const QString&)));
  connect(  memoryView,     SIGNAL(registers()),
            dbgController,  SLOT(slotRegisters()));
  connect(  memoryView,     SIGNAL(libraries()),
            dbgController,  SLOT(slotLibraries()));

  connect(  dbgController,  SIGNAL(rawGDBMemoryDump(char*)),
            memoryView,     SLOT(slotRawGDBMemoryView(char*)));
  connect(  dbgController,  SIGNAL(rawGDBDisassemble(char*)),
            memoryView,     SLOT(slotRawGDBMemoryView(char*)));
  connect(  dbgController,  SIGNAL(rawGDBRegisters(char*)),
            memoryView,     SLOT(slotRawGDBMemoryView(char*)));
  connect(  dbgController,  SIGNAL(rawGDBLibraries(char*)),
            memoryView,     SLOT(slotRawGDBMemoryView(char*)));

  memoryView->exec();
  delete memoryView;
}

void CKDevelop::slotDebugStatus(const QString& msg, int state)
{
  QString stateIndicator("P");    // default to "paused"

  if (state & s_appBusy)
  {
    stateIndicator = "A";
    edit_widget->clearStepLine();
  }

  if (state & (s_dbgNotStarted|s_appNotStarted))
    stateIndicator = " ";

  if (state & s_programExited)
  {
    stateIndicator = "E";
    edit_widget->clearStepLine();
  }

  statusBar()->changeItem(stateIndicator, ID_STATUS_DBG);

  if (!msg.isEmpty())
    slotStatusMsg(msg);
}

void CKDevelop::slotDebugAttach()
{
  QDir::setCurrent(prj->getRunFromDir());
  if (dbgInternal)
  {
    QString libtool     = prj->getLibtool();;
    QString binProgram  = prj->getExecutable();

    if (dbgController)
      slotDebugStop();

    slotStatusMsg(i18n("Debug running process..."));
    // Display a dialog with a list of available processes that
    // th debugger can attach to.
    Dbg_PS_Dialog psDlg(this, "process");
    if (psDlg.exec())
    {
      if (int pid = psDlg.pidSelected())
      {
        slotStatusMsg(i18n("Attach to process %1 in %2").arg(pid).arg(dbgExternalCmd));
        setupInternalDebugger();
        dbgController->slotStart(binProgram, QString(), libtool);
        dbgController->slotAttachTo(pid);
        QDir::setCurrent(prj->getProjectDir());
      }
    }
  }
  else
    slotBuildDebug();   // Starts a debugger (external in this case)
}


void CKDevelop::slotDebugExamineCore()
{
  QString runFromDir = prj->getRunFromDir();
  QDir::setCurrent(runFromDir);
  if (dbgInternal)
  {
    QString libtool     = prj->getLibtool();;
    QString binProgram  = prj->getExecutable();

    if (dbgController)
      slotDebugStop();

    slotStatusMsg(i18n("Enter core file to examine..."));

    if (project)
    {
      if (QString coreFile = KFileDialog::getOpenFileName(prj->getProjectDir(),"core"))
      {
        slotStatusMsg(i18n("Examine core file %1 in %2").arg(coreFile).arg(dbgExternalCmd));
        setupInternalDebugger();
        dbgController->slotStart(binProgram, QString(), libtool);
        dbgController->slotCoreFile(coreFile);
//        QDir::setCurrent(prj->getProjectDir());
     }
    }
  }
  else
    slotBuildDebug();   // Starts a debugger (external in this case)
}

void CKDevelop::slotDebugNamedFile()
{
  if (dbgInternal)
  {
    if (dbgController)
      slotDebugStop();

    slotStatusMsg(i18n("Enter executable to debug..."));
    if (project)
    {
      if (QString debugFile = KFileDialog::getOpenFileName(prj->getProjectDir(),"*"))
      {
        slotStatusMsg(i18n("Debugging %1 in %2").arg(debugFile).arg(dbgExternalCmd));
        setupInternalDebugger();
        QDir::setCurrent(debugFile);
        dbgController->slotStart(debugFile, prj->getDebugArgs());
        brkptManager->slotSetPendingBPs();
        slotDebugRun();
      }
    }
  }
  else
    slotBuildDebug();   // Starts a debugger (external in this case)
}

// I need this one for the accel keys
void CKDevelop::slotBuildDebugStart()
{
  slotBuildDebug();
}

void CKDevelop::slotBuildDebug(bool bWithArgs)
{
  KConfig *config=KGlobal::config();
  bool isDirty=isProjectDirty();
  int qYesNoCancel=0;
  int rebuildType;

  config->setGroup("MakeOptionsSettings");
  rebuildType=config->readNumEntry("RebuildType", 0);
  if (rebuildType==0 && isDirty)
    qYesNoCancel=QMessageBox::warning(this,i18n("Project sources have been modified"),
                    i18n("Should the project be rebuild before starting the debug session?"),
                    i18n("Yes"), i18n("No"), i18n("Cancel"),0,2);

  if (qYesNoCancel!=2)
  {
//    if(!bKDevelop)
//      switchToKDevelop();

    beep=false;
    prj->writeProject();
    if (rebuildType==2 || (isDirty && qYesNoCancel==0))
    {
      if (bWithArgs)
        next_job="debug_with_args";
      else
        next_job="debug";
      slotBuildMake();
    }
    else
    {
      if (bWithArgs)
        slotStartDebugRunWithArgs();
      else
        slotStartDebug();
    }
  }
}

void CKDevelop::slotDebugRunWithArgs()
{
  slotBuildDebug(true);
}

void CKDevelop::slotStartDebugRunWithArgs()
{
  QDir::setCurrent(prj->getRunFromDir());
  QString libtool     = prj->getLibtool();;
  QString binProgram  = prj->getExecutable();

  QString args=prj->getDebugArgs();
  if (args.isEmpty())
    args=prj->getExecuteArgs();

  CExecuteArgDlg argdlg(this,i18n("Arguments"),i18n("Debug with arguments"), args);
  if (argdlg.exec())
  {
    args = argdlg.getArguments();
    prj->setDebugArgs(args);    
    prj->writeProject();

    stdin_stdout_widget->clear();
    stderr_widget->clear();

    slotStatusMsg(i18n("Debugging %1 (with arg %2 %3)")
                        .arg(prj->getBinPROGRAM())
                        .arg(args)
                        .arg(libtool.isEmpty()? "" : " with libtool"));

    setupInternalDebugger();
    dbgController->slotStart(binProgram, args, libtool);
    brkptManager->slotSetPendingBPs();
    slotDebugRun();
  }
}

void CKDevelop::slotStartDebug()
{
  QString runFromDir = prj->getRunFromDir();
  QDir::setCurrent(runFromDir);
  QString libtool     = prj->getLibtool();;
  QString binProgram  = prj->getExecutable();

  // if we can run the application, so we can clear the Makefile.am-changed-flag
  prj->clearMakefileAmChanged();

  if (dbgInternal)
  {
    if (dbgController)
      slotDebugStop();

    stdin_stdout_widget->clear();
    stderr_widget->clear();

    slotStatusMsg(i18n("Debugging %1 (from %2 %3) in internal debugger")
                          .arg(prj->getBinPROGRAM())
                          .arg(runFromDir)
                          .arg(libtool.isEmpty()? "" : " with libtool")
);
    setupInternalDebugger();
    dbgController->slotStart(binProgram, QString(), libtool);
    brkptManager->slotSetPendingBPs();
    slotDebugRun();
    return;
  }

  if(!CToolClass::searchProgram(dbgExternalCmd)){
    return;
  }

  slotStatusMsg(i18n("Debugging %1 (from %2) in %3")
                    .arg(prj->getBinPROGRAM())
                    .arg(runFromDir)
                    .arg(dbgExternalCmd));

  KShellProcess process("/bin/sh");
  process << dbgExternalCmd+ " " + binProgram;
  process.start(KProcess::DontCare);
}

void CKDevelop::setDebugMenuProcess(bool enable)
{
  setToolMenuProcess(!enable);
  bool onOff = dbgInternal && enable;

  toolBar()->setItemEnabled(ID_DEBUG_RUN,                   onOff);
  toolBar()->setItemEnabled(ID_DEBUG_STEP,                  onOff);
  toolBar()->setItemEnabled(ID_DEBUG_NEXT,                  onOff);
  toolBar()->setItemEnabled(ID_DEBUG_FINISH,                onOff);

  debug_menu->setItemEnabled(ID_DEBUG_RUN,                  onOff);
  debug_menu->setItemEnabled(ID_DEBUG_RUN_CURSOR,           onOff);
  debug_menu->setItemEnabled(ID_DEBUG_NEXT,                 onOff);
  debug_menu->setItemEnabled(ID_DEBUG_NEXT_INST,            onOff);
  debug_menu->setItemEnabled(ID_DEBUG_STEP,                 onOff);
  debug_menu->setItemEnabled(ID_DEBUG_STEP_INST,            onOff);
  debug_menu->setItemEnabled(ID_DEBUG_FINISH,               onOff);
  debug_menu->setItemEnabled(ID_DEBUG_MEMVIEW,              onOff);
  debug_menu->setItemEnabled(ID_DEBUG_BREAK_INTO,           onOff);
  debug_menu->setItemEnabled(ID_DEBUG_STOP,                 enable);

  // now create/destroy the floating toolbar
  if (onOff && dbgController && dbgEnableFloatingToolbar)
  {
    dbgToolbar = new DbgToolbar(dbgController, this);
    dbgToolbar->show();
    connect(  dbgController,  SIGNAL(dbgStatus(const QString&,int)),
              dbgToolbar,     SLOT(slotDbgStatus(const QString&,int)));
  }
  else
  {
    // Always try and delete this when the toolbar is disabled
    delete dbgToolbar;
    dbgToolbar = 0;
  }
}


void CKDevelop::setupInternalDebugger()
{
  ASSERT(!dbgController);
  if (dbgController)
    return;
  
  saveTimer->stop();  // stop the autosaving
//  slotStatusMsg(i18n("Running %1 (from %2) in internal debugger").arg(prj->getBinPROGRAM()).arg(prj->getRunFromDir()));

  dbgController = new GDBController(var_viewer->varTree(), frameStack);
  dbgShuttingDown = false;
  setDebugMenuProcess(true);  // MUST be after dbgController

  o_tab_view->setTabEnabled("FStackTab", dbgInternal && dbgController);
  o_tab_view->setTabEnabled("DisassembleTab", dbgInternal && dbgController);
  t_tab_view->setTabEnabled("VARTab", dbgInternal && dbgController);
  frameStack->setEnabled(dbgInternal && dbgController);
  disassemble->setEnabled(dbgInternal && dbgController);
  var_viewer->setEnabled(dbgInternal && dbgController);

  connect(  dbgController,    SIGNAL(rawGDBBreakpointList (char*)),
            brkptManager,     SLOT(slotParseGDBBrkptList(char*)));
  connect(  dbgController,    SIGNAL(rawGDBBreakpointSet(char*, int)),
            brkptManager,     SLOT(slotParseGDBBreakpointSet(char*, int)));
  connect(  dbgController,    SIGNAL(acceptPendingBPs()),
            brkptManager,     SLOT(slotSetPendingBPs()));
  connect(  dbgController,    SIGNAL(unableToSetBPNow(int)),
            brkptManager,     SLOT(slotUnableToSetBPNow(int)));

  connect(  dbgController,    SIGNAL(dbgStatus(const QString&,int)),
            this,             SLOT(slotDebugStatus(const QString&,int)));
  connect(  dbgController,    SIGNAL(showStepInSource(const QString&,int, const QString&)),
            this,             SLOT(slotDebugShowStepInSource(const QString&,int, const QString&)));
  connect(  dbgController,    SIGNAL(ttyStdout(const char*)),
            this,             SLOT(slotApplReceivedStdout(const char*)));
  connect(  dbgController,    SIGNAL(ttyStderr(const char*)),
            this,             SLOT(slotApplReceivedStderr(const char*)));

#if defined(GDB_MONITOR) || defined(DBG_MONITOR)
  connect(  dbgController,    SIGNAL(rawData(const QString&)),
            this,             SLOT(slotDebugReceivedStdout(const QString&)));
#endif

  connect(  brkptManager,     SIGNAL(publishBPState(Breakpoint*)),
            dbgController,    SLOT(slotBPState(Breakpoint*)));
  connect(  brkptManager,     SIGNAL(clearAllBreakpoints()),
            dbgController,    SLOT(slotClearAllBreakpoints()));

  connect(  frameStack,       SIGNAL(selectFrame(int)),
            dbgController,    SLOT(slotSelectFrame(int)));

  connect(  var_viewer->varTree(),  SIGNAL(expandItem(VarItem*)),
            dbgController,          SLOT(slotExpandItem(VarItem*)));
  connect(  var_viewer->varTree(),  SIGNAL(expandUserItem(VarItem*, const QCString&)),
            dbgController,          SLOT(slotExpandUserItem(VarItem*, const QCString&)));
  connect(  var_viewer->varTree(),  SIGNAL(setLocalViewState(bool)),
            dbgController,          SLOT(slotSetLocalViewState(bool)));

  connect(  header_widget,    SIGNAL(runToCursor(const QString&, int)),
            dbgController,    SLOT(slotRunUntil(const QString&, int)));

  connect(  cpp_widget,       SIGNAL(runToCursor(const QString&, int)),
            dbgController,    SLOT(slotRunUntil(const QString&, int)));

  connect(  disassemble,    SIGNAL(disassemble(const QString&, const QString&)),
            dbgController,  SLOT(slotDisassemble(const QString&, const QString&)));
  connect(  dbgController,  SIGNAL(showStepInSource(const QString&,int, const QString&)),
            disassemble,    SLOT(slotShowStepInSource(const QString&,int, const QString&)));
  connect(  dbgController,  SIGNAL(rawGDBDisassemble(char*)),
            disassemble,    SLOT(slotDisassemble(char*)));

  slotTCurrentTab(VAR);
  slotTTabSelected(VAR);
}

void CKDevelop::slotBuildMake(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  // reset to the project's root directory
  QString defaultMakefileDir = prj->getProjectDir();
  QDir::setCurrent(defaultMakefileDir);

  // Kill the debugger if it's running
  if (dbgController)
    slotDebugStop();

  //save/generate dialog if needed
  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);
  setToolMenuProcess(false);
  slotFileSaveAll();
  slotStatusMsg(i18n("Running make..."));
  messages_widget->clear();

  // get the path where make will run
  QString makefileDir = prj->getDirWhereMakeWillBeCalled(defaultMakefileDir);
  if (makefileDir == "./")
    makefileDir = prj->getProjectDir(); // avoid a "." subdir
  if (makefileDir[0] != '/')  // in case of a relative path
    makefileDir = prj->getProjectDir() + makefileDir; // complete the path to an absolute one

  // set the path where make will run
  QDir::setCurrent(makefileDir);
  error_parser->setStartDir(makefileDir);

  // check if there's a Makefile, if not: grumble about it
  if (!QFileInfo(makefileDir + "Makefile").exists()) {
    QString makefileGenerator;
    if (prj->getProjectType() != "normal_empty")
      makefileGenerator = i18n(" by Build->Configure!\n");
    else
      makefileGenerator = i18n(". Possibly by tmake?\n");
    QMessageBox::warning(this,i18n("Makefile not found"),
                         i18n("You want to make (by running 'make') in\n\n") +
                         makefileDir +
                         i18n("\n\nbut there is no Makefile in this directory.\n\n") +
                         i18n("Hints:\n") +
                         i18n("1. Possibly you forgot to create the Makefiles.\n") +
                         i18n("   In that case create them") + makefileGenerator +
                         i18n("2. Or this directory does not belong to your project.\n") +
                         i18n("   Check the settings in Project->Options->MakeOptions!"));
    setToolMenuProcess(true);
    return;
  }

  // set the make arguments
  process.clearArguments();
  if (prj->getProjectType()!="normal_empty")
  {
    QString flaglabel=
          ((prj->getProjectType()=="normal_c") ? "CFLAGS=\"" : "CXXFLAGS=\"") +
          prj->getCXXFLAGS() + prj->getAdditCXXFLAGS() + "\"";

    process << flaglabel;

//    if (!prj->getCXXFLAGS().isEmpty() || !prj->getAdditCXXFLAGS().isEmpty())
//    {
//      if (!prj->getCXXFLAGS().isEmpty())
//        process << prj->getCXXFLAGS();
//      if (!prj->getAdditCXXFLAGS().isEmpty())
//        process << prj->getAdditCXXFLAGS();
//    }

    process  << "LDFLAGS=\""+prj->getLDFLAGS()+"\"";

//   if (!prj->getLDFLAGS().isEmpty())
//                process << prj->getLDFLAGS();
//   process  << "\"";
  }

  // feed the shell process with a make command and possibly with some arguments
//  if(!prj->getMakeOptions().isEmpty())
    process << make_cmd << prj->getMakeOptions();
//  else
//    process << make_cmd;

  beep = true;

  // start make
  if (next_job.isEmpty())
    next_job="make_end";

  process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
}

// void CKDevelop::slotBuildMakeWith(){
//   KLineEditDlg *box = new KLineEditDlg(i18n("Make with :"), make_with_cmd.data(), this, true);
//   box->show();

//   if (!box->result())   /* cancelled */
//     return;

//   make_with_cmd = box->text();
//   delete box;

//   showOutputView(true);
//   setToolMenuProcess(false);
//   slotFileSaveAll();
//   slotStatusMsg(i18n("Running make..."));
//   messages_widget->clear();

//   if ( prj->getProjectType() == "normal_empty" ||
//        prj->getProjectType() == "normal_java")
//     QDir::setCurrent(prj->getProjectDir()); 
//   else
//     QDir::setCurrent(prj->getProjectDir() + prj->getSubDir()); 

//   process.clearArguments();
//   process << make_with_cmd;

//   beep = true;
//   process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
// }

void CKDevelop::slotBuildRebuildAll(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  // reset to the project's root directory
  QString defaultMakefileDir = prj->getProjectDir();
  QDir::setCurrent(defaultMakefileDir);

  slotDebugStop();
  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);

  setToolMenuProcess(false);
  slotFileSaveAll();
  slotStatusMsg(i18n("Running make clean-command and rebuilding..."));
  messages_widget->clear();

  // get the path where rebuild will run
  QString makefileDir = prj->getDirWhereMakeWillBeCalled(defaultMakefileDir);
  if (makefileDir == "./")
    makefileDir = prj->getProjectDir(); // avoid a "." subdir
  if (makefileDir[0] != '/')  // in case of a relative path
    makefileDir = prj->getProjectDir() + makefileDir; // complete the path to an absolute one

  // set the path where rebuild will run
  QDir::setCurrent(makefileDir);
  error_parser->setStartDir(makefileDir);

  // check if there's a Makefile, if not: grumble about it
  if (!QFileInfo(makefileDir + "Makefile").exists()) {
    QString makefileGenerator;
    if (prj->getProjectType() != "normal_empty")
      makefileGenerator = i18n(" by Build->Configure!\n");
    else
      makefileGenerator = i18n(". Possibly by tmake?\n");
    QMessageBox::warning(this,i18n("Makefile not found"),
                         i18n("You want to rebuild (by running 'make clean;make') in\n\n") +
                         makefileDir +
                         i18n("\n\nbut there is no Makefile in this directory.\n\n") +
                         i18n("Hints:\n") +
                         i18n("1. Possibly you forgot to create the Makefiles.\n") +
                         i18n("   In that case create them") + makefileGenerator +
                         i18n("2. Or this directory does not belong to your project.\n") +
                         i18n("   Check the settings in Project->Options->MakeOptions!"));
    setToolMenuProcess(true);
    return;
  }
  else {
    process.clearArguments();
    process << make_cmd;
    process << "clean";
    next_job = make_cmd; // checked in slotProcessExited()
    beep = true;
    process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
  }
}

void CKDevelop::slotBuildCleanRebuildAll(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  // reset to the project's root directory
  QString defaultMakefileDir = prj->getProjectDir();
  QDir::setCurrent(defaultMakefileDir);

  prj->updateMakefilesAm();
  slotDebugStop();
  //  QString shell = getenv("SHELL");
  QString flaglabel;
  //  if(shell == "/bin/bash"){
      flaglabel=(prj->getProjectType()=="normal_c") ? "CFLAGS=\"" : "CXXFLAGS=\"";
      //  }
      //  else{
      //      flaglabel=(prj->getProjectType()=="normal_c") ? "env CFLAGS=\"" : "env CXXFLAGS=\"";
      //  }
  
  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);
  setToolMenuProcess(false);
  slotFileSaveAll();
  messages_widget->clear();
  slotStatusMsg(i18n("Running make clean and rebuilding all..."));

  // get the path where make distclean&make will run
  QString makefileDir = prj->getDirWhereMakeWillBeCalled(defaultMakefileDir);
  if (makefileDir == "./")
    makefileDir = prj->getProjectDir(); // avoid a "." subdir
  if (makefileDir[0] != '/')  // in case of a relative path
    makefileDir = prj->getProjectDir() + makefileDir; // complete the path to an absolute one

  // set the path where make distclean&make will run
  QDir::setCurrent(makefileDir);
  error_parser->setStartDir(makefileDir);

  // check if there's a Makefile, if not: grumble about it
  QString makefile("Makefile.dist");
  if (!QFileInfo(makefileDir + makefile).exists()) {
    makefile="Makefile.cvs";
    if (!QFileInfo(makefileDir + makefile).exists()) {
      makefile="Makefile";
      if (!QFileInfo(makefileDir + makefile).exists()) {
        QString makefileGenerator;
        if (prj->getProjectType() != "normal_empty")
          makefileGenerator = i18n(" by Build->Configure!\n");
        else
          makefileGenerator = i18n(". Possibly by tmake?\n");
        QMessageBox::warning(this, QString(i18n(makefile)) + i18n(" not found"),
                             i18n("You want to distclean and build (by running 'make distclean;make') in\n\n") +
                             makefileDir +
                             i18n("\n\nbut there is no ") + i18n(makefile) + i18n(" in this directory.\n\n") +
                             i18n("Hints:\n") +
                             i18n("1. Possibly you forgot to create them") + makefileGenerator +
                             i18n("2. Or this directory does not belong to your project.\n") +
                             i18n("   Check the settings in Project->Options->MakeOptions!"));
        setToolMenuProcess(true);
        return;
      }
    }
  }
  makefile = "Makefile.dist";
  if (!QFileInfo(prj->getProjectDir() + makefile).exists())
    makefile="Makefile.cvs";

  // running make -f Makefile.dist/cvs
  shell_process.clearArguments();
  shell_process << make_cmd << "distclean && " << " cd " << prj->getProjectDir() << " && " << make_cmd
    << " -f "+makefile+" && ";
  shell_process << flaglabel;
  if (!prj->getCXXFLAGS().isEmpty() || !prj->getAdditCXXFLAGS().isEmpty())
  {
    if (!prj->getCXXFLAGS().isEmpty())
      shell_process << prj->getCXXFLAGS().simplifyWhiteSpace () << " ";
    if (!prj->getAdditCXXFLAGS().isEmpty())
      shell_process << prj->getAdditCXXFLAGS().simplifyWhiteSpace ();
  }
  shell_process  << "\" " << "LDFLAGS=\" " ;
  if (!prj->getLDFLAGS().isEmpty())
    shell_process << prj->getLDFLAGS().simplifyWhiteSpace ();
  //  shell_process  << "\" "<< "./configure && " << make_cmd;
  QDir::setCurrent(prj->getProjectDir()); // configure is only in the project's root directory
  shell_process  << "\" "<< "./configure " << prj->getConfigureArgs() << " && " << make_cmd;
  beep = true;

  next_job="make_end";
  shell_process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
}

void CKDevelop::slotBuildDistClean(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  // reset to the project's root directory
  QString defaultMakefileDir = prj->getProjectDir();
  QDir::setCurrent(defaultMakefileDir);

  slotDebugStop();
  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);
  setToolMenuProcess(false);
  slotFileSaveAll();
  slotStatusMsg(i18n("Running make distclean..."));
  messages_widget->clear();

  // get the path where make distclean will run
  QString makefileDir = prj->getDirWhereMakeWillBeCalled(defaultMakefileDir);
  if (makefileDir == "./")
    makefileDir = prj->getProjectDir(); // avoid a "." subdir
  if (makefileDir[0] != '/')  // in case of a relative path
    makefileDir = prj->getProjectDir() + makefileDir; // complete the path to an absolute one

  // set the path where make distclean will run
  QDir::setCurrent(makefileDir);
  error_parser->setStartDir(makefileDir);

  // check if there's a Makefile, if not: grumble about it
  if (!QFileInfo(makefileDir + "Makefile").exists()) {
    QString makefileGenerator;
    if (prj->getProjectType() != "normal_empty")
      makefileGenerator = i18n(" by Build->Configure!\n");
    else
      makefileGenerator = i18n(". Possibly by tmake?\n");
    QMessageBox::warning(this,i18n("Makefile not found"),
                         i18n("You want to distclean (by running 'make distclean') in\n\n") +
                         makefileDir +
                         i18n("\n\nbut there is no Makefile in this directory.\n\n") +
                         i18n("Hints:\n") +
                         i18n("1. Possibly you forgot to create the Makefiles.\n") +
                         i18n("   In that case create them") + makefileGenerator +
                         i18n("2. Or this directory does not belong to your project.\n") +
                         i18n("   Check the settings in Project->Options->MakeOptions!"));
    setToolMenuProcess(true);
    return;
  }
  else {
    // run distclean
    process.clearArguments();
    process << make_cmd << "distclean";
    process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
  }
}

void CKDevelop::slotBuildMakeClean(){
  if(!CToolClass::searchProgram(make_cmd)){
    return;
  }

  slotDebugStop();
  error_parser->reset();
  error_parser->toogleOn();
  showOutputView(true);
  setToolMenuProcess(false);
  slotFileSaveAll();
  slotStatusMsg(i18n("Running make clean..."));
  messages_widget->clear();

  // reset to the project's root directory
  QString defaultMakefileDir = prj->getProjectDir();
  QDir::setCurrent(defaultMakefileDir);

  // get the path where make clean will run
  QString makefileDir = prj->getDirWhereMakeWillBeCalled(defaultMakefileDir);
  if (makefileDir == "./")
    makefileDir = prj->getProjectDir(); // avoid a "." subdir
  if (makefileDir[0] != '/')  // in case of a relative path
    makefileDir = prj->getProjectDir() + makefileDir; // complete the path to an absolute one

  // set the path where make will run
  QDir::setCurrent(makefileDir);
  error_parser->setStartDir(makefileDir);

  // check if there's a Makefile, if not: grumble about it
  if (!QFileInfo(makefileDir + "Makefile").exists()) {
    QString makefileGenerator;
    if (prj->getProjectType() != "normal_empty")
      makefileGenerator = i18n(" by Build->Configure!\n");
    else
      makefileGenerator = i18n(". Possibly by tmake?\n");
    QMessageBox::warning(this,i18n("Makefile not found"),
                         i18n("You want to clean up (by running 'make clean') in\n\n") +
                         makefileDir +
                         i18n("\n\nbut there is no Makefile in this directory.\n\n") +
                         i18n("Hints:\n") +
                         i18n("1. Possibly you forgot to create the Makefiles.\n") +
                         i18n("   In that case create them") + makefileGenerator +
                         i18n("2. Or this directory does not belong to your project.\n") +
                         i18n("   Check the settings in Project->Options->MakeOptions!"));
    setToolMenuProcess(true);
    return;
  }
  else {
    // Cool - there's a Makefile. Now run make clean
    error_parser->setStartDir(makefileDir);
    process.clearArguments();
    process << make_cmd << "clean";
    process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
  }
}

void CKDevelop::slotBuildAutoconf(){
  if(!CToolClass::searchProgram("automake")){
    return;
  }
  if(!CToolClass::searchProgram("autoconf")){
    return;
  }

  slotDebugStop();
  showOutputView(true);
  error_parser->toogleOff();
  setToolMenuProcess(false);
  slotFileSaveAll();
  slotStatusMsg(i18n("Running autoconf/automake suite..."));
  messages_widget->clear();
  QDir::setCurrent(prj->getDirWhereMakeWillBeCalled(prj->getProjectDir()));
  error_parser->setStartDir(prj->getDirWhereMakeWillBeCalled(prj->getProjectDir()));

  shell_process.clearArguments();
  QString makefile("Makefile.dist");
  if(!QFileInfo(QDir::current(), makefile).exists())
    makefile="Makefile.cvs";

  shell_process << make_cmd << " -f "+makefile;

  shell_process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
  beep = true;
}


void CKDevelop::slotBuildConfigure(){
    //    QString shell = getenv("SHELL");

  QString args=prj->getConfigureArgs();
  CExecuteArgDlg argdlg(this,i18n("Arguments"),i18n("Configure with Arguments"),args);
  if(argdlg.exec()){
    prj->setConfigureArgs(argdlg.getArguments());    
    prj->writeProject();
  
  } else {
    return;
  }
  slotDebugStop();

  slotStatusMsg(i18n("Running ./configure..."));
  QString flaglabel;
  /* This condition only works on Linux systems */
  //  if(shell == "/bin/bash"){
      flaglabel=(prj->getProjectType()=="normal_c") ? "CFLAGS=\"" : "CXXFLAGS=\"";
      //  }
      //  else{
      //      flaglabel=(prj->getProjectType()=="normal_c") ? "env CFLAGS=\"" : "env CXXFLAGS=\"";
      //  }

  showOutputView(true);
  setToolMenuProcess(false);
  error_parser->toogleOff();
  messages_widget->clear();
  slotFileSaveAll();
  QDir::setCurrent(prj->getProjectDir());
  shell_process.clearArguments();
  shell_process << flaglabel;
  if (!prj->getCXXFLAGS().isEmpty() || !prj->getAdditCXXFLAGS().isEmpty())
  {
      if (!prj->getCXXFLAGS().isEmpty())
          shell_process << prj->getCXXFLAGS().simplifyWhiteSpace () << " ";
      if (!prj->getAdditCXXFLAGS().isEmpty())
          shell_process << prj->getAdditCXXFLAGS().simplifyWhiteSpace ();
  }
  shell_process  << "\" " << "LDFLAGS=\" " ;
  if (!prj->getLDFLAGS().isEmpty())
         shell_process << prj->getLDFLAGS().simplifyWhiteSpace ();
  shell_process  << "\" "<< "./configure " << argdlg.getArguments();
  shell_process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
  beep = true;
}


void CKDevelop::slotBuildStop(){
  slotStatusMsg(i18n("Killing current process..."));
  slotDebugStop();
  setToolMenuProcess(true);
  process.kill();
  shell_process.kill();
  appl_process.kill();
  slotStatusMsg(i18n("Ready."));
}


///////////////////////////////////////////////////////////////////////////////////////
// TOOLS-Menu slots
///////////////////////////////////////////////////////////////////////////////////////


void CKDevelop::slotToolsTool(int tool){

  if(!CToolClass::searchProgram(tools_exe.at(tool)) ){
    return;
  }
//  if(!bKDevelop)
//    switchToKDevelop();
    
//  showOutputView(false);

  QString argument=tools_argument.at(tool);
     
  // This allows us to replace the macro %H with the header file name, %S with the source file name
  // and %D with the project directory name.  Any others we should have?
  argument.replace( QRegExp("%H"), header_widget->getName() );
  argument.replace( QRegExp("%S"), cpp_widget->getName() );
  if(project){
    argument.replace( QRegExp("%D"), prj->getProjectDir() );
  }
  QString process_call;
  if(argument.isEmpty()){
      process_call=tools_exe.at(tool);
   } else {
      process_call=tools_exe.at(tool)+argument;
   }
  KShellProcess process("/bin/sh");
  process << process_call;
  process.start(KProcess::DontCare);
}



///////////////////////////////////////////////////////////////////////////////////////
// OPTIONS-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

void CKDevelop::slotOptionsEditor(){
  slotStatusMsg(i18n("Setting up the Editor..."));
  cpp_widget->optDlg();
  config->setGroup("KWrite Options");
  cpp_widget->writeConfig(config);
  cpp_widget->doc()->writeConfig(config);
  header_widget->copySettings(cpp_widget);
  config->setGroup("KWrite Options");
  header_widget->readConfig(config);
  header_widget->doc()->readConfig(config);
  slotStatusMsg(i18n("Ready."));

}
void CKDevelop::slotOptionsEditorColors(){
  slotStatusMsg(i18n("Setting up the Editor's colors..."));
  cpp_widget->colDlg();
  config->setGroup("KWrite Options");
  cpp_widget->writeConfig(config);
  cpp_widget->doc()->writeConfig(config);
  header_widget->copySettings(cpp_widget);
  config->setGroup("KWrite Options");
  header_widget->readConfig(config);
  header_widget->doc()->readConfig(config);
  slotStatusMsg(i18n("Ready."));

}


void CKDevelop::slotOptionsSyntaxHighlightingDefaults(){
  slotStatusMsg(i18n("Setting up syntax highlighting default colors..."));
  cpp_widget->hlDef();
  config->setGroup("KWrite Options");
  cpp_widget->writeConfig(config);
  cpp_widget->doc()->writeConfig(config);
  header_widget->copySettings(cpp_widget);
  config->setGroup("KWrite Options");
  header_widget->readConfig(config);
  header_widget->doc()->readConfig(config);
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotOptionsSyntaxHighlighting(){
  slotStatusMsg(i18n("Setting up syntax highlighting colors..."));
  cpp_widget->hlDlg();
  config->setGroup("KWrite Options");
  cpp_widget->writeConfig(config);
  cpp_widget->doc()->writeConfig(config);
  header_widget->copySettings(cpp_widget);
  config->setGroup("KWrite Options");
  header_widget->readConfig(config);
  header_widget->doc()->readConfig(config);
  slotStatusMsg(i18n("Ready."));
}
void CKDevelop::slotOptionsDocBrowser(){
   slotStatusMsg(i18n("Configuring Documentation Browser..."));

   CDocBrowserOptionsDlg browserOptions;

   connect( browserOptions.fontOptions, SIGNAL(fontSize(int)),
     browser_widget, SLOT(slotDocFontSize( int )) );
   connect( browserOptions.fontOptions, SIGNAL(standardFont( const QString& )),
     browser_widget, SLOT(slotDocStandardFont( const QString& )) );
   connect( browserOptions.fontOptions, SIGNAL(fixedFont( const QString& )),
     browser_widget, SLOT(slotDocFixedFont( const QString& )) );
   connect( browserOptions.colorOptions, SIGNAL(colorsChanged(const QColor&, const QColor&,
      const QColor&, const QColor&, const bool, const bool)),
     browser_widget, SLOT(slotDocColorsChanged(const QColor&, const QColor&,
                const QColor&, const QColor&, const bool, const bool)) );

   browserOptions.show();
   slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotOptionsToolsConfigDlg(){
  slotStatusMsg(i18n("Configuring Tools-Menu entries..."));
  CToolsConfigDlg* configdlg= new CToolsConfigDlg(this,"configdlg");
  configdlg->show();

  tools_menu->clear();
  setToolmenuEntries();
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotOptionsSpellchecker(){

}

void CKDevelop::slotOptionsConfigureEnscript(){
  if (!CToolClass::searchProgram("enscript")) {
    return;
  }
  enscriptconf = new CConfigEnscriptDlg(this, "confdialog");
  enscriptconf->resize(610,510);
  enscriptconf->exec();
  delete (enscriptconf);
}

void CKDevelop::slotOptionsConfigureA2ps(){
  if (!CToolClass::searchProgram("a2ps")) {
    return;
  }
  a2psconf = new CConfigA2psDlg(this, "confdialog");
  a2psconf->resize(600,430);
  a2psconf->exec();
  delete (a2psconf);
}

void CKDevelop::slotOptionsKDevelop(){
  slotStatusMsg(i18n("Setting up KDevelop..."));

  CKDevSetupDlg* setup= new CKDevSetupDlg(accel,this,"Setup");
  // setup->show();
  if (setup->exec())
  {
    if (setup->hasChangedPath())
    {
     doc_tree->changePathes();
     doc_tree->refresh(prj);
    }
  }

  delete setup;

  // This might have changed
  initDebugger();

  accel->readSettings();
  setKeyAccel();
  slotStatusMsg(i18n("Ready."));
}
// slots needed by the KDevelop Setup
void CKDevelop::slotOptionsMake(){
  config->setGroup("General Options");
  make_cmd=config->readEntry("Make","make");

}

void CKDevelop::slotOptionsAutosave(bool autosave){

  bAutosave=autosave;
  if(bAutosave)
    saveTimer->start(saveTimeout);
  else
    saveTimer->stop();
}

void CKDevelop::slotOptionsAutosaveTime(int time){

  switch(time){
  case 0:
    saveTimeout=3*60*1000;
    break;
  case 1:
    saveTimeout=5*60*1000;
    break;
  case 2:
    saveTimeout=15*60*1000;
    break;
  case 3:
    saveTimeout=30*60*1000;
    break;
  }
  saveTimer->changeInterval(saveTimeout);
}

void CKDevelop::slotOptionsAutoswitch(bool autoswitch){
  bAutoswitch=autoswitch;
}

void CKDevelop::slotOptionsDefaultCV(bool defaultcv){
  bDefaultCV=defaultcv;
}

void CKDevelop::slotOptionsUpdateKDEDocumentation(){
  if(!CToolClass::searchProgram("kdoc")){
    return;
  }
  slotStatusMsg(i18n("Updating KDE-Libs documentation..."));
  config->setGroup("Doc_Location");
  CUpdateKDEDocDlg dlg(&shell_process, config, this,"test");
  if(dlg.exec()){
    slotStatusMsg(i18n("Generating Documentation..."));
    setToolMenuProcess(false);
    if (dlg.isUpdated())
    {
        config->writeEntry("doc_kde",dlg.getDocPath());
        config->sync();
        // doc_tree->refresh(prj);
        // doing this by next_job ... if the documentation generation has finished
  next_job="doc_refresh";
    }
  }
}
void CKDevelop::slotOptionsCreateSearchDatabase(){
  bool foundGlimpse = CToolClass::searchInstProgram("glimpseindex");
  bool foundHtDig = CToolClass::searchInstProgram("htdig");
  if(!foundGlimpse && !foundHtDig){
    KMessageBox::error( 0,
                        i18n("KDevelop needs either \"glimpseindex\" or \"htdig\" to work properly.\n\tPlease install one!"),
                        i18n("Program not found!"));
    return;
  }
  CCreateDocDatabaseDlg dlg(this,"DLG",&shell_process,config,foundGlimpse, foundHtDig);
  if(dlg.exec()){
    slotStatusMsg(i18n("Creating Search Database..."));
  }

  return;

}

///////////////////////////////////////////////////////////////////////////////////////
// BOOKMARK-Menu slots
///////////////////////////////////////////////////////////////////////////////////////

/*  
void CKDevelop::slotBookmarksSet(){
  if(s_tab_view->getCurrentTab()==BROWSER)
    slotBookmarksAdd();
  else{
    if(edit_widget==header_widget)
      header_widget->setBookmark();
    if(edit_widget==cpp_widget)
      cpp_widget->setBookmark();
  }
}
*/

void CKDevelop::slotBookmarksToggle()
{
  if(s_tab_view->getCurrentTab()==BROWSER)
  {
    doc_bookmarks->clear();

    // Check if the current URL is bookmarked
    int pos = doc_bookmarks_list.find(browser_widget->currentURL());
    if(pos > -1)
    {
      // The current URL is bookmarked, let's remove the bookmark
      doc_bookmarks_list.remove(pos);
      doc_bookmarks_title_list.remove(pos);
    }
    else
    {
      // The current URL is not bookmark, let's bookmark it
      doc_bookmarks_list.append(browser_widget->currentURL());
      doc_bookmarks_title_list.append(browser_widget->currentTitle());
    }
    
    // Recreate thepopup menu
    for (uint i = 0 ; i < doc_bookmarks_list.count(); i++){
      doc_bookmarks->insertItem(SmallIconSet("html"),doc_bookmarks_title_list.at(i));
    }
  }
  else
  {
    if(edit_widget==header_widget)
      header_widget->toggleBookmark();
    if(edit_widget==cpp_widget)
      cpp_widget->toggleBookmark();
  }
}

void CKDevelop::slotBookmarksClear(){
  if(s_tab_view->getCurrentTab()==BROWSER){
    doc_bookmarks_list.clear();
    doc_bookmarks_title_list.clear();
    doc_bookmarks->clear();
  }    
  else{
    if(edit_widget==header_widget)
      header_widget->clearBookmarks();
    if(edit_widget==cpp_widget)
      cpp_widget->clearBookmarks();
  }  
}

void CKDevelop::openBrowserBookmark(const QString& file)
{
  slotStatusMsg(i18n("Opening bookmark..."));
  slotURLSelected(file,1,"test");
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotBookmarksBrowserSelected(int id_)
{
  openBrowserBookmark(doc_bookmarks_list.at(id_));
}  

void CKDevelop::slotBookmarksNext()
{
  if(s_tab_view->getCurrentTab()==BROWSER)
  {
    if(doc_bookmarks_list.count() > 0)
    {
      QString file = doc_bookmarks_list.next();
      if (file.isEmpty())
        file = doc_bookmarks_list.first();
      openBrowserBookmark(file);  
    }
  }
  else
  {
    edit_widget->nextBookmark();
  }
}

void CKDevelop::slotBookmarksPrevious()
{
  if(s_tab_view->getCurrentTab()==BROWSER)
  {
    if(doc_bookmarks_list.count() > 0)
    {
      QString file = doc_bookmarks_list.prev();
      if(file.isEmpty())
        file = doc_bookmarks_list.last();
      openBrowserBookmark(file);  
    }
  }
  else
  {
    edit_widget->previousBookmark();
  }
}

///////////////////////////////////////////////////////////////////////////////////////
// HELP-Menu slots
///////////////////////////////////////////////////////////////////////////////////////
void CKDevelop::slotHelpBack()
{
  slotStatusMsg(i18n("Switching to last page..."));
  QString str = history_list.prev();
  if (str != 0){
    s_tab_view->setCurrentTab(BROWSER);
    browser_widget->showURL(str);
  }

  //KDEBUG1(KDEBUG_INFO,CKDEVELOP,"COUNT HISTORYLIST: %d",history_list.count());
  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotHelpForward()
{
  slotStatusMsg(i18n("Switching to next page..."));
  QString str = history_list.next();
  if (str != 0){
    s_tab_view->setCurrentTab(BROWSER);
    browser_widget->showURL(str);
  }

  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotHelpHistoryBack(int id)
{
  slotStatusMsg(i18n("Opening history page..."));

	int index = history_prev->indexOf(id);
  QString str = history_list.at(index);
  if (str != 0){
    s_tab_view->setCurrentTab(BROWSER);
    browser_widget->showURL(str);
  }

  slotStatusMsg(i18n("Ready."));

}

void CKDevelop::slotHelpHistoryForward( int id)
{
  slotStatusMsg(i18n("Opening history page..."));

	int index = history_next->indexOf(id);
  int cur=history_list.at()+1;
  QString str = history_list.at(cur+index);
  if (str != 0){
    s_tab_view->setCurrentTab(BROWSER);
    browser_widget->showURL(str);
  }

  slotStatusMsg(i18n("Ready."));
}

void CKDevelop::slotHelpBrowserReload()
{
  slotStatusMsg(i18n("Reloading page..."));
  s_tab_view->setCurrentTab(BROWSER);
  browser_widget->view()->setFocus();
  browser_widget->showURL(browser_widget->currentURL(), true);
  slotStatusMsg(i18n("Ready."));
}

//*****************************************************************************
// void encodeURL(String &str)
//   Convert a normal string to a URL 'safe' string.  This means that
//   all characters not explicitly mentioned in the URL BNF will be
//   escaped.  The escape character is '%' and is followed by 2 hex
//   digits representing the octet.
//
QString encodeURL(const QString &str)
{
    QString  temp;
    static const char  *digits = "0123456789ABCDEF";
    const char  *p;

    for (p = str; p && *p; p++)
    {
      if (isascii(*p) && (isdigit(*p) || isalpha(*p)))
        temp += *p;
      else
      {
        temp += '%';
        temp += digits[(*p >> 4) & 0x0f];
        temp += digits[*p & 0x0f];
      }
    }
    return temp;
}

void CKDevelop::slotHelpSearchText(QString text){
  int pos;

  useGlimpse = CToolClass::searchInstProgram("glimpse");
  useHtDig = CToolClass::searchInstProgram("htsearch");

  if (!useGlimpse && !useHtDig)
  {
    KMessageBox::error(0,
                        i18n("KDevelop needs either \"glimpse\" or \"htsearch\" to work properly.\n\tPlease install one!"),
                        i18n("Program not found!"));
    return;
  }


  /// stripping error causing \n's
  if(!text.isEmpty())
  {
    text.replace(QRegExp("^\n"), "");
    pos=text.find("\n");
    if (pos>-1)
      text=text.left(pos);

    text.replace(QRegExp("'"), "'\\''"); // handle ' in a right way
  }

  if(text.isEmpty()){
    KMessageBox::error(this,i18n("You must select a text for searching the documentation!"));
    return;
  }
  //  cerr << ":" << text << ":" << endl;

  doc_search_display_text = text.copy(); // save the text
  text=realSearchText2regExp(text);  // change the text for using with regexp
  doc_search_text = text.copy();

  config->setGroup("Doc_Location");
  QString engine=config->readEntry("searchengine","htdig");

  slotStatusMsg(i18n("Searching selected text in documentation..."));
  if(engine=="glimpse" && useGlimpse && !QFile::exists(locateLocal("appdata", ".glimpse_index")))
  {
    if (!useHtDig) {
      if(KMessageBox::questionYesNo(this,
                    i18n("KDevelop couldn't find the search database.\n Do you want to generate it now?"),
                    i18n("Error...")) == KMessageBox::Yes)
      {
        slotOptionsCreateSearchDatabase();
      }
      return;
    }
    useGlimpse = false;
  }
  enableCommand(ID_HELP_BROWSER_STOP);
  search_output = ""; // delete all from the last search
  search_process.clearArguments();
  if (engine=="glimpse" && useGlimpse && QFile::exists(locateLocal("appdata", ".glimpse_index")))
  {
    search_process << "glimpse";
    search_process << "-H" << locateLocal("appdata", "");
    search_process << "-U" << "-c" << "-y" << "'" + text +"'";

    search_process.start(KShellProcess::NotifyOnExit,KShellProcess::AllOutput);
  }
  if (useHtDig && engine=="htdig" )
  {
    search_process << "htsearch -c " +
                        locate("appdata", "tools/htdig.conf") +
                        " \"format=&matchesperpage=30&words=" +
                        encodeURL(text) +"\" | sed -e '/file:\\/\\/localhost/s//file:\\/\\//g' > " +
                        locateLocal("appdata", "search_result.html");
    search_process.start(KShellProcess::NotifyOnExit,KShellProcess::AllOutput);
  }
}

void CKDevelop::slotHelpSearchText()
{
  QString text;
  if(s_tab_view->getCurrentTab()==BROWSER){
    text = browser_widget->selectedText();
  }
  else{
    text = edit_widget->markedText();
    if(text == ""){
      text = edit_widget->currentWord();
    }
  }

  slotHelpSearchText(text);
}

void CKDevelop::slotHelpSearch()
{
  slotStatusMsg(i18n("Searching for Help on..."));
  CFindDocTextDlg* help_srch_dlg=new CFindDocTextDlg(this,"Search_for_Help_on");
  connect(help_srch_dlg,SIGNAL(signalFind(QString)),this,SLOT(slotHelpSearchText(QString)));
  help_srch_dlg->exec();
  delete help_srch_dlg;
}


void CKDevelop::showDocHelp(const QString& filename)
{
  QString file = DocTreeKDevelopBook::locatehtml(filename);
  if (!file.isEmpty())
    slotURLSelected(file, 1, "test");
}

void CKDevelop::slotHelpContents(){
  showDocHelp("index.html");
}

void CKDevelop::slotHelpProgramming(){
  showDocHelp("programming/index.html");
}

void CKDevelop::slotHelpTutorial(){
  showDocHelp("tutorial/index.html");
}

void CKDevelop::slotHelpKDELibRef(){
  showDocHelp("kde_libref/index.html");
}

void CKDevelop::slotHelpReference() {
  showDocHelp("reference/C/cref.html");
}

void CKDevelop::slotHelpTipOfDay(){
  KTipofDay* tipdlg=new KTipofDay(this, "tip of the day");
  tipdlg->show();

  delete tipdlg;  
}

void CKDevelop::slotHelpHomepage(){
    new KRun("http://www.kdevelop.org");
}


void CKDevelop::slotHelpAPI(){
  if(project){
    QString api_file=prj->getProjectDir() + prj->getProjectName().lower() +  "-api/index.html";
     //MB
     if (doctool_menu->isItemChecked(ID_PROJECT_DOC_TOOL_DOXYGEN))
     {
       QString api_dir =  prj->getProjectDir() + "/";
      QString doxconf = api_dir +  prj->getProjectName().lower()+".doxygen";
      if(!QFileInfo(doxconf).exists())
      {
           KMessageBox::error(0,
                       i18n("Doxygen configuration file not found\n"
                             "Generate a valid one:\n"    
                             "Project->API Doc Tool->Configure doxygen"));
        return;
      }
      api_file=api_dir +  prj->getProjectName().lower() +"-api/html/index.html";
   }
    //MB end
    if(!QFileInfo(api_file).exists()){
//      int result=KMessageBox::yesNo( this, i18n("No Project API documentation !"), i18n("The Project API documentation is not present.\n" 
//                                                                      "Would you like to generate it now ?"), KMessageBox::QUESTION);
//      if(result==1){
//        slotProjectAPI();
//      }
//      else{
        return;  // replaced by right mouse button handling to generate the API in the DocTreeView
//      }
    }
    else{
      slotStatusMsg(i18n("Switching to project API Documentation..."));
      slotURLSelected(api_file,1,"test");
      slotStatusMsg(i18n("Ready.")); 
    }
  }
}
void CKDevelop::slotHelpManual(){
  if(project){

    QString name = prj->getSGMLFile().copy();
    QFileInfo finfo(name);
 
    QString doc_file = finfo.dirPath() + "/" + finfo.baseName()+ ".html";
    if(!QFileInfo(doc_file).exists()){
      // try docbook file projectdir/doc/HTML/index.html
      doc_file = prj->getProjectDir()+"/doc/en/HTML/index.html";
      if(!QFileInfo(doc_file).exists())
        return;   // replaced by right mouse button handling to generate the help manual in DocTreeView
      else
        slotURLSelected(doc_file,1,"test");
    }
    else{
      slotStatusMsg(i18n("Switching to project Manual..."));
      slotURLSelected(doc_file,1,"test");
      slotStatusMsg(i18n("Ready."));
    }
  }
}


/*********************************************************************
 *                                                                   *
 *             SLOTS FOR THE CLASSBROWSER WIZARD BUTTON              *
 *                                                                   *
 ********************************************************************/

/*---------------------------- CKDevelop::slotClassbrowserViewClass()
 * slotClassbrowserViewClass()
 *   Event when the user wants to view the classdeclaration from the
 *   browser toolbar.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserViewClass()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  QString classname = classCombo->currentText();

  CVGotoDeclaration( classname, "", THCLASS, THCLASS );
}

/*---------------------------- CKDevelop::slotClassbrowserViewDeclaration()
 * slotClassbrowserViewDeclaration()
 *   Event when the user wants to view a declaration from the
 *   browser toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserViewDeclaration()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  KComboBox* methodCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_METHOD_CHOICE);
  QString classname = classCombo->currentText();
  QString methodname = methodCombo->currentText();

  if(classname==i18n("(Globals"))
      CVGotoDeclaration( classname, methodname, THFOLDER, THGLOBAL_FUNCTION );
  else
    CVGotoDeclaration( classname, methodname, THCLASS, THPUBLIC_METHOD );
}

/*----------------------- CKDevelop::slotClassbrowserViewDefinition()
 * slotClassbrowserViewDefinition()
 *   Event when the user wants to view a definition from the  browser
 *   toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserViewDefinition()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  KComboBox* methodCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_METHOD_CHOICE);
  QString classname = classCombo->currentText();
  QString methodname = methodCombo->currentText();

  if( methodname.isEmpty() && !classname==i18n("(Globals") ){
      CVGotoDefinition( classname, "", THCLASS, THCLASS );
  }
  else{
    if(classname==i18n("(Globals"))
      CVGotoDefinition( classname, methodname, THFOLDER, THGLOBAL_FUNCTION );
    else
      CVGotoDefinition( classname, methodname, THCLASS, THPUBLIC_METHOD );
  }
}

/*------------------------------ CKDevelop::slotClassbrowserNewMethod()
 * slotClassbrowserNewMethod()
 *   Event when the user wants to create a new method from the browser
 *   toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserNewMethod()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  QString classname = classCombo->currentText();

  if( !classname.isEmpty() )
  {
// TODO
//    // Make class the current one
//    CParsedClass *aClass;
//    aClass = class_tree->store->getClassByName( classname );
//
//    // and do the work
//    class_tree->slotMethodNew();
  }
}

/*------------------------- CKDevelop::slotClassbrowserNewAttribute()
 * slotClassbrowserNewAttribute()
 *   Event when the user wants to create a new attribute from the 
 *   browser toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserNewAttribute()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  QString classname = classCombo->currentText();

  if( !classname.isEmpty() )
  {
// TODO
//    // Make class the current one
//    CParsedClass *aClass;
//    aClass = class_tree->store->getClassByName( classname );
//
//    // and do the work
//    class_tree->slotAttributeNew();
  }
}

/*------------------------- CKDevelop::slotClassbrowserNewSignal()
 * slotClassbrowserNewSignal()
 *   Event when the user wants to create a new signal from the
 *   browser toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserNewSignal()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  QString classname = classCombo->currentText();

  if( !classname.isEmpty() )
  {
// TODO
//    // Make class the current one
//    CParsedClass *aClass;
//    aClass = class_tree->store->getClassByName( classname );
//
//    // and do the work
//    class_tree->slotSignalNew();
  }
}

/*------------------------- CKDevelop::slotClassbrowserNewSlot()
 * slotClassbrowserNewSlot()
 *   Event when the user wants to create a new slot from the
 *   browser toolbar/menu.
 *
 * Parameters:
 *   -
 * Returns:
 *   -
 *-----------------------------------------------------------------*/
void CKDevelop::slotClassbrowserNewSlot()
{
  KComboBox* classCombo = toolBar(ID_BROWSER_TOOLBAR)->getCombo(ID_CV_TOOLBAR_CLASS_CHOICE);
  QString classname = classCombo->currentText();

  if( !classname.isEmpty() )
  {
// TODO
//    // Make class the current one
//    CParsedClass *aClass;
//    aClass = class_tree->store->getClassByName( classname );
//
    // and do the work
//    class_tree->slotSlotNew();
  }
}

/////////////////////////////////////////////////////////////////////
// Other slots and functions needed
/////////////////////////////////////////////////////////////////////

void CKDevelop::slotStatusMsg(const char *text)
{
  ///////////////////////////////////////////////////////////////////
  // change status message permanently
//  statusBar()->clear();
//    statProg->hide();
    statProg->reset();
    m_statusLabel->setText(text );
}


void CKDevelop::slotStatusHelpMsg(const char *text)
{
  ///////////////////////////////////////////////////////////////////
    // change status message of whole statusbar temporary (text, msec)
    statusBar()->message(text, 2000);
//    QTimer::singleShot ( 2000, statProg, SLOT(hide()) );

}

void CKDevelop::enableCommand(int id_)
{
  menuBar()->setItemEnabled(id_,true);
  accel->setItemEnabled(id_,true);

//  menuBar()->setItemEnabled(id_,true);
  toolBar()->setItemEnabled(id_,true);
  toolBar(ID_BROWSER_TOOLBAR)->setItemEnabled(id_,true);
}

void CKDevelop::disableCommand(int id_)
{
  menuBar()->setItemEnabled(id_,false);
  accel->setItemEnabled(id_,false);

//  menuBar()->setItemEnabled(id_,false);
  toolBar()->setItemEnabled(id_,false);
  toolBar(ID_BROWSER_TOOLBAR)->setItemEnabled(id_,false);
}

void CKDevelop::slotNewStatus()
{
  int config;
  config = edit_widget->config();
  statusBar()->changeItem(config & cfOvr ? "OVR" : "INS",ID_STATUS_INS_OVR);
  // set new caption... maybe the file content is changed
  setMainCaption();
}

void CKDevelop::slotCPPMarkStatus(KWriteView *,bool bMarked)
{

  int item=s_tab_view->getCurrentTab();
  if (item==CPP)
  {
    if(bMarked){
      enableCommand(ID_EDIT_CUT);
      enableCommand(ID_EDIT_COPY);
    }
    else{
      disableCommand(ID_EDIT_CUT);
      disableCommand(ID_EDIT_COPY);
    }
  }    
}

void CKDevelop::slotHEADERMarkStatus(KWriteView *, bool bMarked)
{
  int item=s_tab_view->getCurrentTab();
  if (item==HEADER)
  {
      if(bMarked){
        enableCommand(ID_EDIT_CUT);
        enableCommand(ID_EDIT_COPY);
      }
      else{
        disableCommand(ID_EDIT_CUT);
        disableCommand(ID_EDIT_COPY);
      }    
  }
}

void CKDevelop::slotBROWSERMarkStatus(KHTMLPart *, bool bMarked)
{
  int item=s_tab_view->getCurrentTab();
  if (item==BROWSER)
  {
      if(bMarked){
        enableCommand(ID_EDIT_COPY);
      }
      else{
        disableCommand(ID_EDIT_COPY);
      }    
  }
}

void CKDevelop::slotClipboardChanged(KWriteView *, bool bContents)
{
  int item=s_tab_view->getCurrentTab();
  QString text=QApplication::clipboard()->text();
  if(!bContents || item==BROWSER)
    disableCommand(ID_EDIT_PASTE);
  else
    enableCommand(ID_EDIT_PASTE);
}

void CKDevelop::slotNewLineColumn()
{
  QString linenumber;
  linenumber = i18n("Line: %1 Col: %2").arg(edit_widget->currentLine() +1).arg(edit_widget->currentColumn() +1);
  statusBar()->changeItem(linenumber.data(), ID_STATUS_LN_CLM);
} 
void CKDevelop::slotNewUndo(){
  int state;
  state = edit_widget->undoState();
  //undo
  if(state & 1){
    enableCommand(ID_EDIT_UNDO);
  }
  else{
    disableCommand(ID_EDIT_UNDO);
  }
  //redo
  if(state & 2){
    enableCommand(ID_EDIT_REDO);
  }
  else{
    disableCommand(ID_EDIT_REDO);
  }
  
}


void CKDevelop::slotURLSelected(const QString& url,int,const char*)
{
  if (url.isEmpty())
    return;

  QString url_str = url;

  // add file: directive only if it is an absolute path
  if (url_str.left(1)=="/")
     url_str=QString("file:") + url;

  if(url_str.contains("kdevelop/search_result.html") != 0){
    browser_widget->showURL(url_str,true); // with reload if equal
  }
  else{
    browser_widget->showURL(url_str); // without reload if equal
  }

  QString str = history_list.current();
  //if it's a url-request from the search result jump to the correct point
  if (str.contains("kdevelop/search_result.html")){
    prev_was_search_result=true; // after this time, jump to the searchkey
  }

	if (s_tab_view->getCurrentTab() != BROWSER)
	{
  	s_tab_view->setCurrentTab(BROWSER);
  	browser_widget->view()->setFocus();
	}
}

void CKDevelop::slotURLonURL(const QString& url )
{
        // in some cases KHTMLView return "file:/file:/...."
        //  this will be here workarounded... and also on
        //  showURL in cdocbrowser.cpp
  QString corr=url, url_str=url;

  if (corr.left(6)=="file:/")
    corr=corr.mid(6, corr.length());
  if (corr.left(5)=="file:")
    url_str=corr;

  if ( url_str )
  {
    m_statusLabel->setText(url_str );
  }
  else
  {
    m_statusLabel->setText(i18n("Ready.") );
  }
}

void CKDevelop::slotDocumentDone()
{
  QString actualURL=browser_widget->currentURL();
  QString actualTitle=browser_widget->currentTitle();
  int cur =  history_list.at()+1; // get the current index
  int found =  history_list.find(actualURL); // get the current index
  int pos = actualURL.findRev('#');
  QString url_wo_ref=actualURL; // without ref

  if(prev_was_search_result){
    browser_widget->findTextBegin();
    browser_widget->findTextNext(QRegExp(doc_search_text),true);
  }

  if (s_tab_view->getCurrentTab()==BROWSER)
    setMainCaption(BROWSER);

  if (pos!=-1)
    url_wo_ref = actualURL.left(pos);

  // insert into the history-list
  // the following if-statement isn't necessary, because
  //   slotDocumentDone isn't called in the other cases [use of KFMclient for non file://....htm(l)]
  if (actualURL.left(7) != "http://" && url_wo_ref.right(4).find("htm", FALSE)>-1)
  {
    // http aren't added to the history list ...
    if (found == -1)
    {
      if(cur == 0 )
      {
        history_list.append(actualURL);
        history_title_list.append(actualTitle);
      }
      else
      {
        history_list.insert(cur,actualURL);
        history_title_list.insert(cur, actualTitle);
      }
    }
    else
    {
      // the desired URL was already found in the list
      if (actualURL.contains("kdevelop/search_result.html") && history_title_list.at(found)!=actualTitle)
      {
         // this means... a new search_result.html is selected and an old one
         // was found in list
         //   so append it at the end
         history_list.remove(found);
         history_title_list.remove(found);
         // append now the new one
         cur=history_list.count();
         history_list.insert(cur,actualURL);
         history_title_list.insert(cur, actualTitle);
      }
      else
      {
        if (prev_was_search_result)
        {
          // this means... sort the found entry after the search_result.html-entry
          //   so we can always use the back button to get the last search results
          history_list.remove(found);
          history_title_list.remove(found);
          // correct cur after removing a list element
          if (found<cur)
            cur--;
          history_list.insert(cur,actualURL);
          history_title_list.insert(cur, actualTitle);
        }
        else
          cur=found;
      }
    }

    // set now the pointer of the history list
    history_list.at(cur);

    // reorganize the prev- and the next-historylist
    history_next->clear();
    history_prev->clear();

    int i;
    for ( i =0 ; i < cur; i++)
       history_prev->insertItem(history_title_list.at(i));

    for (i = cur+1 ; i < (int) history_list.count(); i++)
       history_next->insertItem(history_title_list.at(i));

    // disable the back button if were at the start of the list
    if (cur == 0)
      disableCommand(ID_HELP_BACK);
    else
      enableCommand(ID_HELP_BACK);

    // disable the forward button if we're at the end of the list
    if (cur >= ((int) history_list.count())-1)
      disableCommand(ID_HELP_FORWARD);
    else
      enableCommand(ID_HELP_FORWARD);
  }

  prev_was_search_result=false;
  disableCommand(ID_HELP_BROWSER_STOP);
}

void CKDevelop::slotReceivedStdout(KProcess*,char* buffer,int buflen)
{
  messages_widget->insertAtEnd(QCString(buffer,buflen+1));
  o_tab_view->setCurrentTab(MESSAGES);
  // QString str1 = messages_widget->text();

//   if(error_parser->getMode() == CErrorMessageParser::MAKE){
    
//     error_parser->parseInMakeMode(&str1,prj->getProjectDir() + prj->getSubDir());
//   }
//   if(error_parser->getMode() == CErrorMessageParser::SGML2HTML){
//     error_parser->parseInSgml2HtmlMode(&str1,prj->getProjectDir() + prj->getSubDir() + "/docs/en/" + prj->getSGMLFile());
//   }

//   //enable/disable the menus/toolbars
//   if(error_parser->hasNext()){
//     enableCommand(ID_VIEW_NEXT_ERROR);
//   }
//   else{
//     disableCommand(ID_VIEW_NEXT_ERROR);
//   }
  
//   if(error_parser->hasPrev()){
//     enableCommand(ID_VIEW_PREVIOUS_ERROR);
//   }
//   else{
//     disableCommand(ID_VIEW_PREVIOUS_ERROR);
//   }
}
void CKDevelop::slotReceivedStderr(KProcess*,char* buffer,int buflen){
  messages_widget->insertAtEnd(QCString(buffer,buflen+1));
  o_tab_view->setCurrentTab(MESSAGES);
  // QString str1 = messages_widget->text();
//   if(error_parser->getMode() == CErrorMessageParser::MAKE){
//     error_parser->parseInMakeMode(&str1,prj->getProjectDir() + prj->getSubDir());
//   }
//   if(error_parser->getMode() == CErrorMessageParser::SGML2HTML){
//     error_parser->parseInSgml2HtmlMode(&str1,prj->getProjectDir() + prj->getSubDir() + "/docs/en/" + prj->getSGMLFile());
//   }

//   //enable/disable the menus/toolbars
//   if(error_parser->hasNext()){
//     enableCommand(ID_VIEW_NEXT_ERROR);
//   }
//   else{
//     disableCommand(ID_VIEW_NEXT_ERROR);
//   }
  
//   if(error_parser->hasPrev()){
//     enableCommand(ID_VIEW_PREVIOUS_ERROR);
//   }
//   else{
//     disableCommand(ID_VIEW_PREVIOUS_ERROR);
//   }
}
void CKDevelop::slotApplReceivedStdout(KProcess*,char* buffer,int buflen){
  stdin_stdout_widget->insertAtEnd(QCString(buffer,buflen+1));
//  if (*(buffer+buflen-1) == '\n')
//    buflen--;
    
//  QString str(buffer,buflen+1);
//  stdin_stdout_widget->insertLine(str);
//  stdin_stdout_widget->setCursorPosition(stdin_stdout_widget->numLines()-1,0);

//  int x,y;
//  showOutputView(true);
//  stdin_stdout_widget->cursorPosition(&x,&y);
//  QString str(buffer,buflen+1);
//  stdin_stdout_widget->insertAt(str,x,y);
}
void CKDevelop::slotApplReceivedStderr(KProcess*,char* buffer,int buflen){
  stderr_widget->insertAtEnd(QCString(buffer,buflen+1));
//  if (*(buffer+buflen-1) == '\n')
//    buflen--;
    
//  QString str(buffer,buflen+1);
//  stderr_widget->insertLine(str);
//  stderr_widget->setCursorPosition(stderr_widget->numLines()-1,0);

//  int x,y;
//  showOutputView(true);
//  stderr_widget->cursorPosition(&x,&y);
//  QString str(buffer,buflen+1);
//  stderr_widget->insertAt(str,x,y);
}

void CKDevelop::slotApplReceivedStdout(const char* buffer)
{
  slotApplReceivedStdout(0, (char*)buffer, strlen(buffer));
}
 
void CKDevelop::slotApplReceivedStderr(const char* buffer)
{
    slotApplReceivedStderr(0, (char*)buffer, strlen(buffer));
}

#if defined(GDB_MONITOR) || defined(DBG_MONITOR)
void CKDevelop::slotDebugReceivedStdout(const QString& buffer)
{
  dbg_widget->insertAtEnd(buffer);
//  char* buf = (char*)buffer;
//  int buflen = strlen(buf);
//  if (*(buf+buflen-1) == '\n')
//    buflen--;

//  QString str(buf,buflen+1);
//  dbg_widget->insertLine(str);
//  dbg_widget->setCursorPosition(dbg_widget->numLines()-1,0);
}
#else
void CKDevelop::slotDebugReceivedStdout(const QString& )
{ }
#endif

void CKDevelop::slotSearchReceivedStdout(KProcess* /*proc*/,char* buffer,int buflen){
  QCString str(buffer,buflen+1);
  search_output = search_output + QString(str);
}
void CKDevelop::slotSearchProcessExited(KProcess*)
{
  disableCommand(ID_HELP_BROWSER_STOP);

  // Figure out the filename of the file we will create if we are using glimpse,
  // or the file that should have the results created by HtDig.
  QString filename = locateLocal("appdata", "search_result.html");

  // Since we prefer glimspe when we started the search we have to
  // prefer glimpse here as well!!
  if (useGlimpse)
  {
    QStrList list;
    QString str;
    int nextpos;
    int pos=0;
    while((nextpos = search_output.find('\n', pos)) != -1)
    {
      str = search_output.mid(pos,nextpos-pos);
      list.append(str);
      pos = nextpos+1;
    }

    if (list.isEmpty()){

       KMessageBox::information(0,"\"" + doc_search_display_text + i18n("\" not found in documentation!"),
                                  i18n("Not found!"));
      return;
    }

    int max;
    QStrList sort_list;
    QString found_str;

    // sort on the numeric count at the end of each line.
    // The higher the hit count, the earlier it appears in the list.
    for(int i=0;i<30;i++)
    {
      max =0;
      found_str = "";
      for(str = list.first();str != 0;str = list.next()){
        if (searchToolGetNumber(str) >= max){
          found_str = str.copy();
          max = searchToolGetNumber(str);
        }
      }
      if (found_str != ""){
        sort_list.append(found_str);
        list.remove(found_str);
      }
    }

    QFile file(filename);
    QTextStream stream(&file);
    file.open(IO_WriteOnly);

    stream << "<HTML>";
    stream << "<HEAD><TITLE> - " << i18n("Search for: ") << doc_search_display_text;
    stream << "</TITLE></HEAD><H1>Search String: '" << doc_search_display_text << "'</H1><HR><BODY BGCOLOR=\"#ffffff\"><BR> <TABLE><TR><TH>";
    stream << i18n("Title") << "<TH>" << i18n("Hits") << "\n";
    QString numstr;
    for(str = sort_list.first(); str != 0; str = sort_list.next() ){
      stream << "<TR><TD><A HREF=\""+searchToolGetURL(str)+"\">"+
                searchToolGetTitle(str)+"</A><TD>"+
                numstr.setNum(searchToolGetNumber(str)) + "\n";
    }

    stream << "\n</TABLE></BODY></HTML>";

    file.close();
    slotURLSelected(filename, 1,"test");
    return;
  }
  else
  {
    ASSERT (useHtDig);
    filename = locateLocal("appdata", "search_result.html");
    if (QFile::exists(filename))
      slotURLSelected(filename, 1,"test");
  }
}

QString CKDevelop::searchToolGetTitle(QString str){
  int pos = str.find(' ');
  pos = str.find(' ',pos);
  int end_pos = str.findRev(':');
  return str.mid(pos,end_pos-pos);
}

QString CKDevelop::searchToolGetURL(QString str){
  int pos = str.find(' ');
  return str.left(pos);
}

int CKDevelop::searchToolGetNumber(QString str){
  int pos =str.findRev(':');
  QString sub = str.right((str.length()-pos-2));
  return sub.toInt();
}

/*
void CKDevelop::slotKeyPressedOnStdinStdoutWidget(int key){
  char a = key;
  appl_process.writeStdin(&a,1);
}
*/
void CKDevelop::slotClickedOnMessagesWidget(){
  TErrorMessageInfo info;
  int x,y;

  messages_widget->cursorPosition(&x,&y);
  info = error_parser->getInfo(x+1);
  if(info.filename != ""){
//    if(!bKDevelop)
//      switchToKDevelop();
    messages_widget->setCursorPosition(info.makeoutputline,0);
    switchToFile(info.filename,info.errorline-1);
  }
  else{
     XBell(kapp->getDisplay(),100); // not a next found, beep
  }
    // switchToFile(error_filename);
//     edit_widget->setCursorPosition(error_line-1,0);
//     edit_widget->setFocus();
  // int x,y;
//   int error_line;
//   QString text;
//   QString error_line_str;
//   QString error_filename;
//   int pos1,pos2; // positions in the string
//   QRegExp reg(":[0-9]*:"); // is it an error line?, I hope it works

  
 //  text = messages_widget->textLine(x);
//   if((pos1=reg.match(text)) == -1) return; // not an error line

//   // extract the error-line
//   pos2 = text.find(':',pos1+1);
//   error_line_str = text.mid(pos1+1,pos2-pos1-1);
//   error_line = error_line_str.toInt();

//   // extract the filename
//   pos2 = text.findRev(' ',pos1);
//   if (pos2 == -1) {
//     pos2 = 0; // the filename is at the begining of the string
//   }
//   else { pos2++; }

//   error_filename = text.mid(pos2,pos1-pos2);

//   // switch to the file
//   if (error_filename.find('/') == -1){ // it is a file outer the projectdir ?
//     error_filename = prj->getProjectDir() + prj->getSubDir() + error_filename;
//   }
//   if (QFile::exists(error_filename)){
    
    //  }


}
void CKDevelop::slotProcessExited(KProcess* proc){
  setToolMenuProcess(true);
  slotStatusMsg(i18n("Ready."));
  bool ready = true;
  QString result="";
  if (proc->normalExit()) {
    
    result= ((proc->exitStatus()) ? i18n("*** failed ***\n") : 
          i18n("*** success ***\n"));
    if ( proc== &appl_process)
      result.sprintf(i18n("*** exit-code: %i ***\n"), 
         proc->exitStatus());


    if (next_job=="doc_refresh")
    {
      doc_tree->refresh(prj);
      next_job="";
    }

    if (next_job == "make_end"  && process.exitStatus() == 0)
    {
      // if we can run the application, so we can clear the Makefile.am-changed-flag
      prj->clearMakefileAmChanged();
      next_job = "";
    }

    if (next_job == make_cmd)
    { // rest from the rebuild all
      QString makefileDir=prj->getProjectDir() + prj->getSubDir();
      QDir::setCurrent(makefileDir);
      error_parser->setStartDir(makefileDir);
      if (prj->getProjectType()=="normal_empty" &&
       !QFileInfo(makefileDir+"Makefile").exists())
      {
        if (QFileInfo(prj->getProjectDir()+"Makefile").exists())
        {
          QDir::setCurrent(prj->getProjectDir());
          error_parser->setStartDir(prj->getProjectDir());
        }
      }
      process.clearArguments();
      if(!prj->getMakeOptions().isEmpty()){
        process << make_cmd << prj->getMakeOptions();
      }
      else{
        process << make_cmd;
      }
      setToolMenuProcess(false);
      process.start(KProcess::NotifyOnExit,KProcess::AllOutput);
      next_job = "make_end";
      ready=false;
    }

    if (next_job == "debug"  && process.exitStatus() == 0)
    {
      // if we can debug the application, so we can clear the Makefile.am-changed-flag
      prj->clearMakefileAmChanged();

      slotStartDebug();
      next_job = "";
      ready = false;
    }

    if (next_job == "debug_with_args"  && process.exitStatus() == 0)
    {
      // if we can debug the application, so we can clear the Makefile.am-changed-flag
      prj->clearMakefileAmChanged();

      slotStartDebugRunWithArgs();
      next_job = "";
      ready = false;
    }

    if ((next_job == "run"  || next_job == "run_with_args") && process.exitStatus() == 0)
    {
      // if we can run the application, so we can clear the Makefile.am-changed-flag
      prj->clearMakefileAmChanged();

      slotStartRun(next_job=="run_with_args");
      next_job = "";
      ready = false;
    }
      
    if (next_job == "refresh")
    { // rest from the add projectfile
      refreshTrees();
    }
    if (next_job == "fv_refresh")
    { // update fileview trees...
      log_file_tree->refresh(prj);
      real_file_tree->refresh(prj);
    }
    if( next_job == "load_new_prj")
    {

      if(project)    //now that we know that a new project will be built we can close the previous one   {
      {
        QString old_project = prj->getProjectFile();
         if(!slotProjectClose())        //the user may have pressed cancel in which case the state is undetermined
        {
          readProjectFile(old_project);
          slotViewRefresh();
        }
         else
        {
          QDir dir(QDir::current());
          readProjectFile(QDir::currentDirPath()+"/"+ dir.dirName()+".kdevprj");
          slotViewRefresh();    // a new project started, this is legitimate
        }
       }
       else
      {
        QDir dir(QDir::current());
        readProjectFile(QDir::currentDirPath()+"/"+ dir.dirName()+".kdevprj");
        slotViewRefresh();    // a new project started, this is legitimate
      }

    }
    next_job = "";
  }
  else
  {
    result= i18n("*** process exited with error(s) ***\n");
    next_job = "";
  }

  if (!result.isEmpty())
  {
//     int x,y;
//     messages_widget->cursorPosition(&x,&y);
//     messages_widget->insertAt(result, x, y);
    messages_widget->insertAtEnd(result);
  }
  if (ready)
  { // start the error-message parser
    QString str1 = messages_widget->text();
      
    if(error_parser->getMode() == CErrorMessageParser::MAKE){
      error_parser->parseInMakeMode(&str1);
    }
    if(error_parser->getMode() == CErrorMessageParser::SGML2HTML){
//      error_parser->parseInSgml2HtmlMode(&str1, prj->getProjectDir() + prj->getSubDir() + "/docs/en/" + prj->getSGMLFile());
    // docbook file
      error_parser->parseInSgml2HtmlMode(&str1, prj->getSGMLFile());
    }
      //enable/disable the menus/toolbars
    if(error_parser->hasNext()){
      enableCommand(ID_VIEW_NEXT_ERROR);
    }
    else{
      disableCommand(ID_VIEW_NEXT_ERROR);
    }
  }
  if(beep && ready){
      XBell(kapp->getDisplay(),100); //beep :-)
      beep = false;
  }
  
}


void CKDevelop::slotTTabSelected(int item){
  if(item == DOC ){
    // disable the outputview
    //    showOutputView(false);
  }
}
void CKDevelop::slotSTabSelected(int item){
  lasttab = s_tab_view->getCurrentTab();

  if (item == HEADER || item == CPP)
  {
   // enableCommand(ID_FILE_SAVE);  is handled by setMainCaption()
    enableCommand(ID_FILE_SAVE_AS);
    enableCommand(ID_FILE_CLOSE);

    enableCommand(ID_FILE_PRINT);

//  QString text=QApplication::clipboard()->text();
//  if(text.isEmpty())
//    disableCommand(ID_EDIT_PASTE);
//  else
//    enableCommand(ID_EDIT_PASTE);

    enableCommand(ID_EDIT_INSERT_FILE);
    enableCommand(ID_EDIT_SEARCH);
    enableCommand(ID_EDIT_REPEAT_SEARCH);
    enableCommand(ID_EDIT_REPLACE);
    enableCommand(ID_EDIT_SPELLCHECK);
    enableCommand(ID_EDIT_INDENT);
    enableCommand(ID_EDIT_UNINDENT);
    enableCommand(ID_EDIT_COMMENT);
    enableCommand(ID_EDIT_UNCOMMENT);
    enableCommand(ID_EDIT_SELECT_ALL);
    enableCommand(ID_EDIT_DESELECT_ALL);
    enableCommand(ID_EDIT_INVERT_SELECTION);
  }

  if (item == HEADER){
    if(bAutoswitch && t_tab_view->getCurrentTab()==DOC){  
      if ( bDefaultCV)
        t_tab_view->setCurrentTab(CV);
      else
        t_tab_view->setCurrentTab(LFV);
    }
    disableCommand(ID_BUILD_COMPILE_FILE);
    edit_widget = header_widget;
    edit_widget->setFocus();
    slotNewUndo();
    slotNewStatus();
//    setMainCaption();  is called by slotNewStatus()
    slotNewLineColumn();
  }
  if (item == CPP){
    if(bAutoswitch && t_tab_view->getCurrentTab()==DOC){  
      if ( bDefaultCV)
        t_tab_view->setCurrentTab(CV);
      else
        t_tab_view->setCurrentTab(LFV);
    }
    if(project && build_menu->isItemEnabled(ID_BUILD_MAKE)){
      enableCommand(ID_BUILD_COMPILE_FILE);
    }
    edit_widget = cpp_widget;
    edit_widget->setFocus();
    slotNewUndo();
    slotNewStatus();
//    setMainCaption();  is called by slotNewStatus()
    slotNewLineColumn();
  }

  if (item == HEADER || item == CPP)
  {
    int state;
    state = edit_widget->undoState();
    //undo
    if(state & 1)
      enableCommand(ID_EDIT_UNDO);
    else
      disableCommand(ID_EDIT_UNDO);
    //redo
    if(state & 2)
      enableCommand(ID_EDIT_REDO);
    else
      disableCommand(ID_EDIT_REDO);

    QString str = edit_widget->markedText();
    if(str.isEmpty()){
      disableCommand(ID_EDIT_CUT);
      disableCommand(ID_EDIT_COPY);
    }
    else{
      enableCommand(ID_EDIT_CUT);
      enableCommand(ID_EDIT_COPY);
    }    
  }

  if(item == BROWSER)
  {
    disableCommand(ID_BUILD_COMPILE_FILE);

    disableCommand(ID_FILE_SAVE);
    disableCommand(ID_FILE_SAVE_AS);
    disableCommand(ID_FILE_CLOSE);

    disableCommand(ID_FILE_PRINT);

    disableCommand(ID_EDIT_UNDO);
    disableCommand(ID_EDIT_REDO);
    disableCommand(ID_EDIT_CUT);
    disableCommand(ID_EDIT_PASTE);
    disableCommand(ID_EDIT_INSERT_FILE);
    // disableCommand(ID_EDIT_SEARCH);
    // disableCommand(ID_EDIT_REPEAT_SEARCH);
    disableCommand(ID_EDIT_REPLACE);
    disableCommand(ID_EDIT_SPELLCHECK);
    disableCommand(ID_EDIT_INDENT);
    disableCommand(ID_EDIT_UNINDENT);
    disableCommand(ID_EDIT_COMMENT);
    disableCommand(ID_EDIT_UNCOMMENT);
    disableCommand(ID_EDIT_SELECT_ALL);
    disableCommand(ID_EDIT_DESELECT_ALL);
    disableCommand(ID_EDIT_INVERT_SELECTION);
  }

  if(item == BROWSER){
    if(bAutoswitch)
      t_tab_view->setCurrentTab(DOC);
    browser_widget->view()->setFocus();

    if (browser_widget->hasSelection())
      enableCommand(ID_EDIT_COPY);
    else
      disableCommand(ID_EDIT_COPY);

    setMainCaption(BROWSER);
  }
}

void CKDevelop::slotOTabSelected(int item)
{
  if (disassemble)
    disassemble->slotActivate(item == DISASSEMBLE);
  else
    if (item == DISASSEMBLE)
      o_tab_view->setCurrentTab(MESSAGES);
}

void CKDevelop::slotMenuBuffersSelected(int id){
  TEditInfo* info;

  for(info=edit_infos.first();info != 0;info=edit_infos.next()){
    if (info->id == id){
      switchToFile(info->filename);
      return; // if found than return
    }
  }
}


void CKDevelop::slotLogFileTreeSelected(QString file){
  switchToFile(file);
}

void CKDevelop::slotRealFileTreeSelected(QString file){
  switchToFile(file);
}

void CKDevelop::slotUpdateFileFromVCS(QString file){
    switchToFile(file,-1,-1, false,false); // force not reload and no messagebox if modified on disc, because slotFileSave() will do it
    slotFileSave();
    prj->getVersionControl()->update(file);
    switchToFile(file,true,false);
}
void CKDevelop::slotCommitFileToVCS(QString file){
    switchToFile(file,-1,-1,false,false);
    slotFileSave();
    prj->getVersionControl()->commit(file);
    switchToFile(file,true,false);
}

void CKDevelop::slotUpdateDirFromVCS(QString dir){
    slotFileSaveAll();

    prj->getVersionControl()->update(dir);
    TEditInfo* actual_info;
    
    QListIterator<TEditInfo> it(edit_infos); // iterator for edit_infos list

    for ( ; it.current(); ++it ) {
      actual_info = it.current();
      QFileInfo file_info(actual_info->filename);
      if(actual_info->last_modified != file_info.lastModified()){ // reload only changed files
        switchToFile(actual_info->filename,-1,-1,true,false); //force reload, no modified on disc messagebox
      }
    }
}

void CKDevelop::slotCommitDirToVCS(QString dir){
    slotFileSaveAll();
    prj->getVersionControl()->commit(dir);

    TEditInfo* actual_info;
    
    QListIterator<TEditInfo> it(edit_infos); // iterator for edit_infos list

    for ( ; it.current(); ++it ) {
      actual_info = it.current();
      QFileInfo file_info(actual_info->filename);
      if(actual_info->last_modified != file_info.lastModified()){ // reload only changed files
        switchToFile(actual_info->filename,-1,-1,true,false); //force reload, no modified on disc messagebox
      }
    }
    
}
void CKDevelop::slotDocTreeSelected(QString url_file){
  if(url_file == "API-Documentation"){
    slotHelpAPI();
    return;
  }
  if(url_file == "User-Manual"){
    slotHelpManual();
    return;
  }
  QString text = doc_tree->selectedText();
  
  if(!QFile::exists(url_file)){
    if( text == i18n("Qt-Library")){
      if(KMessageBox::questionYesNo(0,
                                    i18n("KDevelop couldn't find the Qt documentation.\n Do you want to set the correct path?"),
                                    i18n("File not found!")) == KMessageBox::Yes) {
        slotOptionsKDevelop();
      }
      return;
    }
    if(text == i18n("KDE-Core-Library")     || text == i18n("KDE-UI-Library") ||
       text == i18n("KDE-KFile-Library")    || text == i18n("KDE-KHTML-Library") ||
       text == i18n("KDE-KFM-Library")      || text == i18n("KDE-KDEutils-Library") ||
       text == i18n("KDE-KAB-Library")      || text == i18n("KDE-KSpell-Library")){
      if(KMessageBox::questionYesNo(0,
                            i18n("KDevelop couldn't find the KDE API-Documentation.\nDo you want to generate it now?"),
                            i18n("File not found!")) == KMessageBox::Yes) {
        slotOptionsUpdateKDEDocumentation();
      }
      return;
    }
  }
  slotURLSelected(url_file, 1, "test");
  
}

void CKDevelop::slotTCurrentTab(int item){
    t_tab_view->setCurrentTab(item);
}

void CKDevelop::slotSCurrentTab(int item){
    s_tab_view->setCurrentTab(item);
}

void CKDevelop::slotToggleLast() {
  if ( lasttab != s_tab_view->getCurrentTab() )
    s_tab_view->setCurrentTab( lasttab );
  else
    switchToFile( lastfile );
}

void CKDevelop::slotBufferMenu( const QPoint& point ) {
  menu_buffers->popup( point );
}

void CKDevelop::slotGrepDialogItemSelected(QString filename,int linenumber){
  switchToFile(filename,linenumber);
}

void CKDevelop::slotToolbarClicked(int item){
  switch (item) {
  case ID_FILE_NEW:
    slotFileNew();
    break;
  case ID_PROJECT_OPEN:
    slotProjectOpen();
    break;
  case ID_FILE_OPEN:
    slotFileOpen();
    break;
  case ID_FILE_SAVE:
    slotFileSave();
    break;
  case ID_FILE_SAVE_ALL:
    slotFileSaveAll();
    break;
  case ID_FILE_PRINT:
    slotFilePrint();
    break;
  case ID_EDIT_UNDO:
    slotEditUndo();
    break;
  case ID_EDIT_REDO:
    slotEditRedo();
    break;
  case ID_EDIT_COPY:
    slotEditCopy();
    break;
  case ID_EDIT_PASTE:
    slotEditPaste();
    break;
  case ID_EDIT_CUT:
    slotEditCut();
    break;
  case ID_VIEW_REFRESH:
    slotViewRefresh();
    break;
  case ID_VIEW_TREEVIEW:
    slotViewTTreeView();
    break;
  case ID_VIEW_OUTPUTVIEW:
    slotViewTOutputView();
    break;
  case ID_BUILD_COMPILE_FILE:
    slotBuildCompileFile();
    break;
  case ID_BUILD_MAKE:
    slotBuildMake();
    break;
  case ID_BUILD_REBUILD_ALL:
    slotBuildRebuildAll();
    break;
  case ID_BUILD_RUN:
    slotBuildRun();
    break;
  case ID_BUILD_STOP:
    slotBuildStop();
    break;
  case ID_TOOLS_DESIGNER:
    startDesigner();
    break;
  case ID_HELP_BACK:
    slotHelpBack();
    break;
  case ID_HELP_FORWARD:
    slotHelpForward();
    break;
  case ID_HELP_BROWSER_RELOAD:
    slotHelpBrowserReload();
    break;
  case ID_HELP_BROWSER_STOP:
#warning FIXME KHTML changes.
//    browser_widget->cancelAllRequests();
    shell_process.kill();
    disableCommand(ID_HELP_BROWSER_STOP);
    break;
  case ID_HELP_CONTENTS:
    slotHelpContents();
    break;
  case ID_HELP_SEARCH_TEXT:
    slotHelpSearchText();
    break;
  case ID_HELP_SEARCH:
    slotHelpSearch();
    break;
  case ID_HELP_WHATS_THIS:
    QWhatsThis::enterWhatsThisMode();
    break;

  case ID_CV_WIZARD:
    // Make the button toggle between declaration and definition.
    if(cv_decl_or_impl){
      slotClassbrowserViewDeclaration();
      cv_decl_or_impl=false;
    }
    else{
      slotClassbrowserViewDefinition();
      cv_decl_or_impl=true;
    }    
    break;

  // Redirect to code that handles menu and toolbar selection
  // for these functions.
  case ID_DEBUG_START:
  case ID_DEBUG_RUN:
  case ID_DEBUG_RUN_CURSOR:
  case ID_DEBUG_STEP:
  case ID_DEBUG_STEP_INST:
  case ID_DEBUG_NEXT:
  case ID_DEBUG_NEXT_INST:
  case ID_DEBUG_STOP:
  case ID_DEBUG_BREAK_INTO:
  case ID_DEBUG_MEMVIEW:
  case ID_DEBUG_FINISH:
    slotDebugActivator(item);
    break;
  }
}

void CKDevelop::statusCallback(int id_){
  switch(id_)
  {
    ON_STATUS_MSG(ID_FILE_NEW,                              i18n("Creates a new file"))
    ON_STATUS_MSG(ID_FILE_OPEN,                             i18n("Opens an existing file"))
    ON_STATUS_MSG(ID_FILE_CLOSE,                            i18n("Closes the actual file"))

    ON_STATUS_MSG(ID_FILE_SAVE,                             i18n("Save the actual document"))
    ON_STATUS_MSG(ID_FILE_SAVE_AS,                          i18n("Save the document as..."))
    ON_STATUS_MSG(ID_FILE_SAVE_ALL,                         i18n("Save all changed files"))

    ON_STATUS_MSG(ID_FILE_PRINT,                            i18n("Prints the current document"))

  //  ON_STATUS_MSG(ID_FILE_CLOSE_WINDOW,i18n("Closes the current window"))

    ON_STATUS_MSG(ID_FILE_QUIT,                             i18n("Exits the program"))

    ON_STATUS_MSG(ID_EDIT_UNDO,                             i18n("Reverts the last editing step"))
    ON_STATUS_MSG(ID_EDIT_REDO,                             i18n("Re-execute the last undone step"))

    ON_STATUS_MSG(ID_EDIT_CUT,                              i18n("Cuts the selected section and puts it to the clipboard"))
    ON_STATUS_MSG(ID_EDIT_COPY,                             i18n("Copys the selected section to the clipboard"))
    ON_STATUS_MSG(ID_EDIT_PASTE,                            i18n("Pastes the clipboard contents to actual position"))

    ON_STATUS_MSG(ID_EDIT_INSERT_FILE,                      i18n("Inserts a file at the current position"))

    ON_STATUS_MSG(ID_EDIT_SEARCH,                           i18n("Searches the file for an expression"))
    ON_STATUS_MSG(ID_EDIT_REPEAT_SEARCH,                    i18n("Repeats the last search"))
    ON_STATUS_MSG(ID_EDIT_REPLACE,                          i18n("Searches and replace expression"))
    ON_STATUS_MSG(ID_EDIT_SEARCH_IN_FILES,                  i18n("Opens the search in files dialog to search for expressions over several files"))

    ON_STATUS_MSG(ID_EDIT_RUN_TO_CURSOR,                    i18n("Run program to this cursor position"))
    ON_STATUS_MSG(ID_EDIT_STEP_OUT_OFF,                     i18n("Run the program until this function/method ends"))
    ON_STATUS_MSG(ID_EDIT_ADD_WATCH_VARIABLE,               i18n("Try to display this variable whenever the application execution is paused"))


    ON_STATUS_MSG(ID_EDIT_INDENT,                           i18n("Moves the selection to the right"))
    ON_STATUS_MSG(ID_EDIT_UNINDENT,                         i18n("Moves the selection to the left"))
    ON_STATUS_MSG(ID_EDIT_COMMENT,                          i18n("Adds // to the beginning of each selected line"))
    ON_STATUS_MSG(ID_EDIT_UNCOMMENT,                        i18n("Removes // from the beginning of each selected line"))

    ON_STATUS_MSG(ID_EDIT_SELECT_ALL,                       i18n("Selects the whole document contents"))
    ON_STATUS_MSG(ID_EDIT_DESELECT_ALL,                     i18n("Deselects the whole document contents"))
    ON_STATUS_MSG(ID_EDIT_INVERT_SELECTION,                 i18n("Inverts the current selection"))


    ON_STATUS_MSG(ID_VIEW_GOTO_LINE,                        i18n("Goes to Line Number..."))
    ON_STATUS_MSG(ID_VIEW_NEXT_ERROR,                       i18n("Switches to the file and line the next error was reported"))
    ON_STATUS_MSG(ID_VIEW_PREVIOUS_ERROR,                   i18n("Switches to the file and line the previous error was reported"))

    ON_STATUS_MSG(ID_VIEW_TREEVIEW,                         i18n("Enables/Disables the treeview"))
    ON_STATUS_MSG(ID_VIEW_OUTPUTVIEW,                       i18n("Enables/Disables the outputview"))

    ON_STATUS_MSG(ID_VIEW_TOOLBAR,                          i18n("Enables/Disables the standard toolbar"))
    ON_STATUS_MSG(ID_VIEW_BROWSER_TOOLBAR,                  i18n("Enables/Disables the browser toolbar"))
    ON_STATUS_MSG(ID_VIEW_STATUSBAR,                        i18n("Enables/Disables the statusbar"))

    ON_STATUS_MSG(ID_VIEW_REFRESH,                          i18n("Refreshes current view"))
    ON_STATUS_MSG(ID_VIEW_IN_KFM,                           i18n("Opens the current document in the KFM browser"))
    ON_STATUS_MSG(ID_PROJECT_KAPPWIZARD,                    i18n("Generates a new project with Application Wizard"))
    ON_STATUS_MSG(ID_PROJECT_OPEN,                          i18n("Opens an existing project"))
    ON_STATUS_MSG(ID_PROJECT_CLOSE,                         i18n("Closes the current project"))
    ON_STATUS_MSG(ID_PROJECT_ADD_FILE_EXIST,                i18n("Adds an existing file to the project"))
    ON_STATUS_MSG(ID_PROJECT_ADD_NEW_TRANSLATION_FILE,      i18n("Adds a new language for internationalization to the project"))
    ON_STATUS_MSG(ID_PROJECT_REMOVE_FILE,                   i18n("Removes file from the project"))

    ON_STATUS_MSG(ID_PROJECT_NEW_CLASS,                     i18n("Creates a new Class frame structure and files"))
    ON_STATUS_MSG(ID_PROJECT_GENERATE,                      i18n("Creates a project file for an existing automake project"))

    ON_STATUS_MSG(ID_PROJECT_FILE_PROPERTIES,               i18n("Shows the current file properties"))
    ON_STATUS_MSG(ID_PROJECT_OPTIONS,                       i18n("Sets project and compiler options"))
    ON_STATUS_MSG(ID_PROJECT_MESSAGES,                      i18n("Invokes make to create the message file by extracting all i18n() macros"))
  //MB
    ON_STATUS_MSG(ID_PROJECT_DOC_TOOL,                      i18n("Switches the documentation tool (kdoc/doxygen)"))
    ON_STATUS_MSG(ID_PROJECT_MAKE_PROJECT_API,              i18n("Creates the Project's API Documentation"))
  //MB end
    ON_STATUS_MSG(ID_PROJECT_MAKE_USER_MANUAL,              i18n("Creates the Project's User Manual with the sgml-file"))
    ON_STATUS_MSG(ID_PROJECT_MAKE_DISTRIBUTION,             i18n("Creates distribution packages from the current project"))
    ON_STATUS_MSG(ID_PROJECT_MAKE_DISTRIBUTION_SOURCE_TGZ,  i18n("Creates a tar.gz file from the current project sources"))

    ON_STATUS_MSG(ID_BUILD_COMPILE_FILE,                    i18n("Compiles the current sourcefile"))
    ON_STATUS_MSG(ID_BUILD_MAKE,                            i18n("Invokes make-command"))
    ON_STATUS_MSG(ID_BUILD_REBUILD_ALL,                     i18n("Rebuilds the program"))
    ON_STATUS_MSG(ID_BUILD_CLEAN_REBUILD_ALL,               i18n("Invokes make clean and rebuild all"))
    ON_STATUS_MSG(ID_BUILD_STOP,                            i18n("Stops make immediately"))
    ON_STATUS_MSG(ID_BUILD_RUN,                             i18n("Invokes make-command and runs the program"))
    ON_STATUS_MSG(ID_BUILD_RUN_WITH_ARGS,                   i18n("Lets you set run-arguments to the binary and invokes the make-command"))
    ON_STATUS_MSG(ID_BUILD_DISTCLEAN,                       i18n("Invokes make distclean and deletes all compiled files"))
    ON_STATUS_MSG(ID_BUILD_MAKECLEAN,                       i18n("Invokes make clean which deletes all object and metaobject files"))
    ON_STATUS_MSG(ID_BUILD_AUTOCONF,                        i18n("Invokes automake and co."))
    ON_STATUS_MSG(ID_BUILD_CONFIGURE,                       i18n("Invokes ./configure"))

    ON_STATUS_MSG(ID_DEBUG_START,                           i18n("Invokes the debugger on the current project executable"))
    ON_STATUS_MSG(ID_DEBUG_START_OTHER,                     i18n("Various startups for the debugger"))
    ON_STATUS_MSG(ID_DEBUG_SET_ARGS,                        i18n("Lets you debug your project app after specifying arguments for your app."))
    ON_STATUS_MSG(ID_DEBUG_CORE,                            i18n("Examine a core file"))
    ON_STATUS_MSG(ID_DEBUG_NAMED_FILE,                      i18n("Debug an app other than the current project executable"))
    ON_STATUS_MSG(ID_DEBUG_ATTACH,                          i18n("Attach to running process"))
    ON_STATUS_MSG(ID_DEBUG_RUN,                             i18n("Continues app execution"))
    ON_STATUS_MSG(ID_DEBUG_RUN_CURSOR,                      i18n("Continues app execution until reaching the current cursor position"))
    ON_STATUS_MSG(ID_DEBUG_STOP,                            i18n("Kills the app and exits the debugger"))
    ON_STATUS_MSG(ID_DEBUG_STEP,                            i18n("Step into"))
    ON_STATUS_MSG(ID_DEBUG_STEP_INST,                       i18n("Step instr"))
    ON_STATUS_MSG(ID_DEBUG_NEXT,                            i18n("Step over"))
    ON_STATUS_MSG(ID_DEBUG_NEXT_INST,                       i18n("Step over instr"))
    ON_STATUS_MSG(ID_DEBUG_FINISH,                          i18n("Run to end of function"))
    ON_STATUS_MSG(ID_DEBUG_MEMVIEW,                         i18n("Various views into the app"))
    ON_STATUS_MSG(ID_DEBUG_BREAK_INTO,                      i18n("Interuppt the app"))

    ON_STATUS_MSG(ID_TOOLS_DESIGNER,                        i18n("Start QT's designer (dialog editor)"))

    ON_STATUS_MSG(ID_OPTIONS_EDITOR,                        i18n("Sets the Editor's behavoir"))
    ON_STATUS_MSG(ID_OPTIONS_EDITOR_COLORS,                 i18n("Sets the Editor's colors"))
    ON_STATUS_MSG(ID_OPTIONS_SYNTAX_HIGHLIGHTING_DEFAULTS,  i18n("Sets the highlighting default colors"))
    ON_STATUS_MSG(ID_OPTIONS_SYNTAX_HIGHLIGHTING,           i18n("Sets the highlighting colors"))
    ON_STATUS_MSG(ID_OPTIONS_DOCBROWSER,                    i18n("Configures the Browser options"))
    ON_STATUS_MSG(ID_OPTIONS_TOOLS_CONFIG_DLG,              i18n("Configures the Tools-Menu entries"))
    ON_STATUS_MSG(ID_OPTIONS_PRINT,                         i18n("Configures printing options"))
    ON_STATUS_MSG(ID_OPTIONS_PRINT_ENSCRIPT,                i18n("Configures the printer to use enscript"))
    ON_STATUS_MSG(ID_OPTIONS_PRINT_A2PS,                    i18n("Configures the printer to use a2ps"))
    ON_STATUS_MSG(ID_OPTIONS_KDEVELOP,                      i18n("Configures KDevelop"))

    ON_STATUS_MSG(ID_BOOKMARKS_SET,                         i18n("Sets a bookmark to the current window file"))
    ON_STATUS_MSG(ID_BOOKMARKS_TOGGLE,                      i18n("Toggles a bookmark to the current window file"))
    ON_STATUS_MSG(ID_BOOKMARKS_NEXT,                        i18n("Goes to the next bookmark in the current window file"))
    ON_STATUS_MSG(ID_BOOKMARKS_PREVIOUS,                    i18n("Goes to the previous bookmark in the current window file"))
    ON_STATUS_MSG(ID_BOOKMARKS_CLEAR,                       i18n("Clears the bookmarks for the current window"))

    ON_STATUS_MSG(ID_HELP_BACK,                             i18n("Switches to last browser page"))
    ON_STATUS_MSG(ID_HELP_FORWARD,                          i18n("Switches to next browser page"))

    ON_STATUS_MSG(ID_HELP_BROWSER_RELOAD,                   i18n("Reloads the current browser page"))
    ON_STATUS_MSG(ID_HELP_BROWSER_STOP,                     i18n("Cancels the document request"))


    ON_STATUS_MSG(ID_HELP_SEARCH_TEXT,                      i18n("Searches the selected text in the documentation"))
    ON_STATUS_MSG(ID_HELP_SEARCH,                           i18n("Lets you search individually for an expression"))

    ON_STATUS_MSG(ID_HELP_CONTENTS,                         i18n("Switches to KDevelop's User Manual"))
    ON_STATUS_MSG(ID_HELP_PROGRAMMING,                      i18n("Switches to the KDevelop Programming Handbook"))
    ON_STATUS_MSG(ID_HELP_TUTORIAL,                         i18n("Switches to the KDE Tutorials Handbook"))
    ON_STATUS_MSG(ID_HELP_KDELIBREF,                        i18n("Switches to the KDE Library Reference Guide Handbook"))
    ON_STATUS_MSG(ID_HELP_KDE2_DEVGUIDE,                    i18n("Switches to the KDE 2 Developerīs Guide Handbook"))
    ON_STATUS_MSG(ID_HELP_REFERENCE,                        i18n("Switches to the C/C++-Reference"))

    ON_STATUS_MSG(ID_HELP_TIP_OF_DAY,                       i18n("Opens the Tip of the Day dialog with hints for using KDevelop"))
    ON_STATUS_MSG(ID_HELP_HOMEPAGE,                         i18n("Enter the KDevelop Homepage"))
    ON_STATUS_MSG(ID_HELP_BUG_REPORT,                       i18n("Sends a bug-report email to the KDevelop Team"))

    ON_STATUS_MSG(ID_HELP_PROJECT_API,                      i18n("Switches to the project's API-Documentation"))
    ON_STATUS_MSG(ID_HELP_USER_MANUAL,                      i18n("Switches to the project's User-Manual"))


    ON_STATUS_MSG(ID_HELP_DLGNOTES,                         i18n("Some information about the dialog editor..."))
    ON_STATUS_MSG(ID_HELP_ABOUT,                            i18n("Programmer's Hall of Fame..."))

    ON_STATUS_MSG(ID_CV_WIZARD,                             i18n("Switches to declaration/implementation"))
    ON_STATUS_MSG(ID_CV_VIEW_DECLARATION,                   i18n("Switches to the method's declaration"))
    ON_STATUS_MSG(ID_CV_VIEW_DEFINITION,                    i18n("Switches to the method's definition"))
    ON_STATUS_MSG(ID_CV_GRAPHICAL_VIEW,                     i18n("Opens the graphical inheritance tree"))
    ON_STATUS_MSG(ID_CV_CLASS_TOOL,                         i18n("Opens the classtool dialog"))
    ON_STATUS_MSG(ID_CV_CLASS_BASE_CLASSES,                 i18n("Displays the inherited classes of the current class"))
    ON_STATUS_MSG(ID_CV_CLASS_DERIVED_CLASSES,              i18n("Displays the classes who inherit the current class"))
    ON_STATUS_MSG(ID_CV_FOLDER_NEW,                         i18n("Creates a new folder"))
    ON_STATUS_MSG(ID_CV_FOLDER_DELETE,                      i18n("Deletes the current folder"))
    ON_STATUS_MSG(ID_CV_CLASS_DELETE,                       i18n("Deletes the current class"))
    ON_STATUS_MSG(ID_CV_VIEW_CLASS_DECLARATION,             i18n("Goes to the class declaration"))
    ON_STATUS_MSG(ID_CV_METHOD_NEW,                         i18n("Opens the New Method dialog"))
    ON_STATUS_MSG(ID_CV_METHOD_DELETE,                      i18n("Deletes the current class method"))
    ON_STATUS_MSG(ID_CV_ATTRIBUTE_NEW,                      i18n("Creates a new attribute for the current class"))
    ON_STATUS_MSG(ID_CV_ATTRIBUTE_DELETE,                   i18n("Deletes the current class attribute"))
    ON_STATUS_MSG(ID_CV_IMPLEMENT_VIRTUAL,                  i18n("Creates a virtual method"))
    ON_STATUS_MSG(ID_CV_ADD_SLOT_SIGNAL,                    i18n("Adds a signal/slot mechanism"))

    // LFV popups
    ON_STATUS_MSG(ID_LFV_NEW_GROUP,                         i18n("Lets you create a new logical file group"))
    ON_STATUS_MSG(ID_LFV_REMOVE_GROUP,                      i18n("Removes the selected logical file group"))
     ON_STATUS_MSG(ID_LFV_GROUP_PROP,                       i18n("Shows the group's properties"))
    ON_STATUS_MSG(ID_LFV_SHOW_PATH_ITEM,                    i18n("Displays the absolute / relative path"))
    ON_STATUS_MSG(ID_FILE_DELETE,                           i18n("Deletes the selected file"))

    // RFV popups
    ON_STATUS_MSG(ID_RFV_SHOW_NONPRJFILES,                  i18n("Show files that aren't registered as project files"))
    ON_STATUS_MSG(ID_PROJECT_CVS_UPDATE,                    i18n("Updates file/directory from repository"))
    ON_STATUS_MSG(ID_PROJECT_CVS_COMMIT,                    i18n("Commits file/directory to the repository"))
    ON_STATUS_MSG(ID_PROJECT_CVS_ADD,                       i18n("Adds file/directory to the repository"))
    ON_STATUS_MSG(ID_PROJECT_CVS_REMOVE,                    i18n("Deletes file from disk and removes it from the repository"))

    default: slotStatusMsg(i18n("Ready"));
  }
}
