# CP/M Image Help

## Open an image

Choose **Open image** and select a disk image. If the format picker appears, compare the suggested definitions or search the catalogue. A plausible directory is evidence for an interpretation. Confirm the disk definition before making edits.

## Browse and export files

Select a numbered user area or choose **All** to browse across user areas. The **User** column identifies each file's area in that view. A CP/M record holds 128 bytes. The logical byte count can include padding from the original import.

With the file list focused, select one file and press **V** or **Return**, or double-click the file. You can also choose **View → View File…**. The viewer reads the current working image, including unsaved changes. Press **T** to switch between text and hex. Press **7** in Text mode to strip bit 7 for WordStar-style files. Text can end at a Ctrl-Z marker in the final CP/M record even if that record contains leftover bytes. Hex shows the original bytes, including bytes after Ctrl-Z. You can select and copy the display. Press **Q** or **Esc** to close the viewer. Previews are limited to 1 MiB.

Opening a Squeezed or Crunched file decodes it when its signature and checksum validate. Use **Q** in the main file list to force Squeeze or **Z** to force Crunch. A failed attempt displays the original bytes. Use **Original** or **Decompressed** to choose the stream in the viewer.

Open an **LBR** or **ARK** to see its members and sizes. Use **Up** and **Down** to select a member, then press **V** or double-click to open it in a separate window. Press **Esc** to return to the archive and the selected member. Each window remains usable after closing its parent. Archives or decoded streams exceeding the 1 MiB preview limit fall back to a bounded raw preview. ARK methods 5–7 can be listed and viewed as stored bytes. Extended ARK headers and CrLZH are outside the supported variants. In a member’s viewer, choose **Export member…** to save the active Original or Decompressed byte stream. For a member that is itself an archive, open **View archive bytes…** first. Text display and Highbit settings do not change the exported bytes.

Choose **File → Export…**, click the Export toolbar icon, or press **X** while either pane has focus. With the file list active, Export copies only the selected files. With the User areas sidebar active, Export copies the selected area into its numbered folder. Select **All** in the sidebar to export all populated areas into separate numbered folders. Use **Tab** or **Shift+Tab** to switch panes without changing the selections.

In the batch export review, check each file you want converted to text. Conversion changes CRLF to LF and stops at the first Ctrl-Z marker. Unchecked files retain their stored bytes.

## Edit and save an image

Import, rename, move, or delete files in the private working image. **Save** publishes changes to the active image. **Save As** publishes to a new destination. **Discard** restores the last saved contents. If Save reports an interrupted transaction, follow the recovery prompt before retrying.

## Create and format images

Use **New Image** to select a disk definition and initialize a filesystem. **Format Image** erases the working filesystem of an open image after confirmation. Reserved system data can be preserved or supplied separately. The changes reach the destination when you save.

## Command line

Use `/img=PATH` to open an image and `/f=FORMAT` to select the definition for subsequent startup images. Use `/diskdefs=PATH` to select the catalogue for the invocation.

For headless operation, use a utility selector such as `/cpmls -f ibm-3740 disk.raw`.
