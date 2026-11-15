# CP/M Image Help

## Open an image

Choose **Open image** and select a disk image. If the format picker appears, compare the suggested definitions or search the catalogue. A plausible directory is evidence for an interpretation. Confirm the disk definition before making edits.

## Browse and export files

Select a numbered user area or choose **All** to browse across user areas. The **User** column identifies each file's area in that view. A CP/M record holds 128 bytes. The logical byte count can include padding from the original import.

With the file list focused, select one file and press **V** or **Return**, or double-click the file. You can also choose **View → View File…**. The viewer displays ASCII and UTF-8 text from the current working image, including unsaved changes. You can select and copy text. Previews are limited to 1 MiB.

Choose **File → Export…**, click the Export toolbar icon, or press **X** while either pane has focus. With the file list active, Export copies only the selected files. With the User areas sidebar active, Export copies the selected area into its numbered folder. Select **All** in the sidebar to export all populated areas into separate numbered folders. Use **Tab** or **Shift+Tab** to switch panes without changing the selections.

## Edit and save an image

Import, rename, move, or delete files in the private working image. **Save** publishes changes to the active image. **Save As** publishes to a new destination. **Discard** restores the last saved contents. If Save reports an interrupted transaction, follow the recovery prompt before retrying.

## Create and format images

Use **New Image** to select a disk definition and initialize a filesystem. **Format Image** erases the working filesystem of an open image after confirmation. Reserved system data can be preserved or supplied separately. The changes reach the destination when you save.

## Command line

Use `/img=PATH` to open an image and `/f=FORMAT` to select the definition for subsequent startup images. Use `/diskdefs=PATH` to select the catalogue for the invocation.

For headless operation, use a utility selector such as `/cpmls -f ibm-3740 disk.raw`.
