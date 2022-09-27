README

Don't post this file to the Wiki.  It's developer information for maintaining
these markup files.

These files are written in Confluence flavored markup so they can be
uploaded to the public wiki directly.
See https://confluence.atlassian.com/doc/confluence-wiki-markup-251003035.html
for syntax.

Many editors have plugins to render previews in this format so check around
for your particular editor.  For Eclipse, the Mylyn WikiText plugin does the
trick.

To upload these files to Confluence, you'll need to edit the corresponding
live page and paste the contents of the edited file.

* Edit the page corresponding to the file that's been edited.
* Delete the contents:
  ** If there's an existing "Wiki Markup" section with markup in it, select
  the contents of the section and delete it. This should leave you with an
  empty "Wiki Markup" section.
  ** If the page is the traditional WYSIWYG Confluence editor, delete the
  entire contents of the page then type "{markup}" and press return.  Now
  delete the "{markup}" placeholder that was automatically added to the
  new "Wiki Markup" section.
* Copy the entire contents of the edited file into your clipboard.
* Back in Confluence, paste the contents of your clipboard into the
  "Wiki Markup" section.
* Preview if you like then save.

