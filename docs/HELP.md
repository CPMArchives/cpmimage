# CP/M Image Help

## Open an image

Choose **Open image** and select a disk image. If the format picker appears, compare the suggested definitions or search the catalogue. A plausible directory is evidence for an interpretation. Confirm the disk definition before making edits.

## Browse and export files

Select a numbered user area or choose **All** to browse across user areas. The **User** column identifies each file's area in that view. A CP/M record holds 128 bytes. The logical byte count can include padding from the original import.

Select files and choose **Export selected**. To export a numbered area into its own folder, choose **Export user area**.

## Edit and save an image

Import, rename, move, or delete files in the private working image. **Save** publishes changes to the active image. **Save As** publishes to a new destination. **Discard** restores the last saved contents. If Save reports an interrupted transaction, follow the recovery prompt before retrying.

## Create and format images

Use **New Image** to select a disk definition and initialize a filesystem. **Format Image** erases the working filesystem of an open image after confirmation. Reserved system data can be preserved or supplied separately. The changes reach the destination when you save.

## Command line

Use `/img=PATH` to open an image and `/f=FORMAT` to select the definition for subsequent startup images. Use `/diskdefs=PATH` to select the catalogue for the invocation.

For headless operation, use a utility selector such as `/cpmls -f ibm-3740 disk.raw`.
