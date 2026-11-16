# CP/M Image Help

## Open an image

Choose **Open image** and select a disk image. If the format picker appears, compare the suggested definitions or search the catalogue. A plausible directory is evidence for an interpretation. Confirm the disk definition before making edits.

## Edit disk format definitions

Open **File → Disk Format Editor…**, or choose **Disk Format Editor…** from the format picker. Use **New** to enter a definition, **Duplicate** to copy a catalogue entry under a new identifier, or **Import diskdefs…** to review a standalone definition. Review any import diagnostics before saving or exporting.

Enter the sector size, track groups, and sectors per track as cpmtools should see them. For a synthetic RAW layout, these groups can be logical. An IMD, JV3, or DMK may supply physical sector IDs and track information, while the definition still supplies the CP/M allocation, directory, and boot settings. **Use mapped sector layout** fills only the sector size, track count, and sectors per track from a verified mapping. CP/M skew applies to the resulting RAW slots.

Use **Validate** to check the definition and **Check against image** to test it against a specific image. Use **Save definition** to add or update the entry in the picker, or **Export diskdefs…** to write a standalone file. To apply a changed definition to an open image, reopen it with the updated entry.

The editor continues to read compatible existing mapping sidecars. Opening, checking, or exporting from an image preserves those sidecars. Use **Preview mapping conversion…** to compare an existing sidecar with the proposed strict form. Confirm the replacement only after reviewing supplied defaults and any metadata that would be removed.

## Browse and export files

Select a numbered user area or choose **All** to browse across user areas. The **User** column identifies each file's area in that view. A CP/M record holds 128 bytes. The logical byte count can include padding from the original import.

With the file list focused, select one file and press **V** or **Return**, or double-click the file. You can also choose **View → View File…**. The viewer reads the current working image, including unsaved changes. Press **T** to switch between text and hex. Press **7** in Text mode to strip bit 7 for WordStar-style files. Text can end at a Ctrl-Z marker in the final CP/M record even if that record contains leftover bytes. Hex shows the original bytes, including bytes after Ctrl-Z. You can select and copy the display. Press **Q** or **Esc** to close the viewer. Previews are limited to 1 MiB.

Opening a Squeezed or Crunched file decodes it when its signature and checksum validate. Use **Q** in the main file list to force Squeeze or **Z** to force Crunch. A failed attempt displays the original bytes. Use **Original** or **Decompressed** to choose the stream in the viewer.

Open an **LBR** or **ARK** to see its members and sizes. Use **Up** and **Down** to select a member, then press **V** or double-click to open it in a separate window. Press **Esc** to return to the archive and the selected member. Each window remains usable after closing its parent. Archives or decoded streams exceeding the 1 MiB preview limit fall back to a bounded raw preview. ARK methods 5–7 can be listed and viewed as stored bytes. Extended ARK headers and CrLZH are outside the supported variants. In a member’s viewer, choose **Export member…** to save the active Original or Decompressed byte stream. For a member that is itself an archive, open **View archive bytes…** first. Text display and Highbit settings do not change the exported bytes. To export several files from the current archive, select their rows, choose **Decompressed when available** or **Original bytes**, and click **Export selected…**. Existing destination names are skipped and reported. A nested archive is exported as one file.

Choose **File → Export…**, click the Export toolbar icon, or press **X** while either pane has focus. With the file list active, Export copies only the selected files. With the User areas sidebar active, Export copies the selected area into its numbered folder. Select **All** in the sidebar to export all populated areas into separate numbered folders. Use **Tab** or **Shift+Tab** to switch panes without changing the selections.

In the batch export review, check each file you want converted to text. Conversion changes CRLF to LF and stops at the first Ctrl-Z marker. Unchecked files retain their stored bytes.

## Edit and save an image

Import, rename, move, or delete files in the private working image. **Save** publishes changes to the active image. **Save As** publishes to a new destination. **Discard** restores the last saved contents. If Save reports an interrupted transaction, follow the recovery prompt before retrying.

## Create and format images

Use **New Image** to select a disk definition and initialize a filesystem. **Format Image** erases the working filesystem of an open image after confirmation. Reserved system data can be preserved or supplied separately. The changes reach the destination when you save.

## Command line

Use `/img=PATH` to open an image and `/f=FORMAT` to select the definition for subsequent startup images. Use `/diskdefs=PATH` to select the catalogue for the invocation.

For headless operation, use a utility selector such as `/cpmls -f ibm-3740 disk.raw`.
