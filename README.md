# Baskı

![Baskı showing a five-page document planned two-sided onto three sheets](screenshot.png)

Baskı prints documents and pictures on a Canon PIXMA E410, one-sided or
two-sided, in color or black and white.

The E410 has no duplexer, and behind CUPS it prints only the first page of a
multi-page job and drops the rest without an error. Baskı works around both:

- **Every page goes out as its own job.** The next page waits until the last
  one has left the queue and the printer has settled, so nothing is dropped.
- **Two-sided printing is guided.** The fronts print first. Then the window
  (and a desktop notification, with a sound) shows an animated sheet turning
  over and the exact steps: take the blank paper out, put back how many sheets,
  which one goes first, and which sheet stays out because its back is blank.
  Press Space and the backs print.

## What it does

- Opens PDF, Word, Excel, PowerPoint and LibreOffice files (through
  LibreOffice) and pictures. Drop a file on the window, press `O`, or pass it
  on the command line: `baski file.pdf`.
- Shows every page; click one to leave it out, double-click to look closer.
  The **Sheets** view shows which page lands on the front and back of each
  sheet, and greys the thumbnails when printing in black and white.
- Page ranges (`1-3, 5`, `4-`, `odd`/`even` buttons), copies (each copy starts
  on a fresh sheet), paper size and type, quality from draft to best.
- Turns landscape pages to fit portrait paper.
- More settings: tone, intensity, contrast, cyan/magenta/yellow balance, which
  cartridges to use, and for the second pass, printing the backs in reverse
  order or upside down.
- A two-sided test sheet with arrows, to find the right second-pass settings
  for how this printer stacks and feeds paper.
- Printer maintenance: nozzle check, head cleaning, head alignment.
- Notices when the printer has gone quiet (usually no paper, or an error light
  waiting for the Resume button) and says so.
- Follows the Omarchy theme and language; English and Turkish are included,
  and the ⋯ menu can pin the app's language.

## Keyboard

| Key | Action |
| --- | --- |
| `O` | Open a file |
| `Space` | Print, or continue with the backs |
| `D` | One-sided / two-sided |
| `C` | Black & white / color |
| `V` | Pages / sheets view |
| `A` | Select every page |
| `+` `-` | More / fewer copies |
| `W` | Close the file |
| `Esc` | Stop printing, close a panel |
| `?` | Show the shortcuts |
| `Q` | Quit (asks while printing) |

## Requirements

A CUPS queue for the printer (the app picks `Canon_E410` when it exists) and
the Canon driver from the AUR, `cnijfilter-e400series`. Baskı renders each page
with the driver's `pstocanonij` filter and sends it raw, so the queue's own
filters do not matter. It also uses `qpdf`, `pdftops`/`pdftoppm` (poppler),
ImageMagick, `notify-send`, and optionally LibreOffice and `canberra-gtk-play`.

## Build

```sh
bin/build      # build/baski
bin/test       # unit tests, offscreen
bin/install    # makepkg -fsi: installs the app and its launcher entry
```
