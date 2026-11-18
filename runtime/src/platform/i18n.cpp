// Language (setting "language", English by default): the game's own text stays English unless lang\<code>.tsv
// and its companions translate it (text_hook.cpp, sys_text.cpp, gs_texrep.cpp); the port's interface is
// German with "de" and English otherwise. The interface texts are written in German in the code;
// tr() returns their English version.
#include "platform.h"
#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace platform {

static const wchar_t* const TEXTS[][2] = {
    // in-game menu (overlay.cpp)
    {L"Einstellungen", L"Settings"},
    {L"An", L"On"},
    {L"Aus", L"Off"},
    {L"Grafik", L"Graphics"},
    {L"MenÃ¼s/Texte im 4:3-Format", L"Menus/text in 4:3 format"},
    {L"GroÃŸe Dialogfenster", L"Large dialogue windows"},
    {L"Untertitel in Zwischensequenzen", L"Subtitles in cutscenes"},
    {L"KÃ¤mpfe bei Gegnerkontakt", L"Battles on monster contact"},
    {L"Undub: japanische Stimmen (ab Neustart)", L"Undub: Japanese voices (after a restart)"},
    {L"Undub aus", L"Undub off"},
    {L"Nach Kartenwechsel automatisch (Slot Auto)", L"Automatically after a map change (slot Auto)"},
    {L"GrÃ¶ÃŸe dieses MenÃ¼s", L"Size of this menu"},
    {L"wird beim Kartenwechsel automatisch beschrieben; zum Speichern einen anderen Slot wÃ¤hlen", L"is written automatically after a map change; choose another slot to save"},
    {L"Kein Abbild der japanischen Disc 1 gewÃ¤hlt.", L"No image of the Japanese disc 1 chosen."},
    {L"Undub: Abbild von Grandia III (Japan) Disc 1 wÃ¤hlen (ISO)", L"Undub: choose the image of Grandia III (Japan) disc 1 (ISO)"},
    {L"Das gewÃ¤hlte Abbild ist nicht Grandia III (Japan) Disc 1.", L"The chosen image is not Grandia III (Japan) disc 1."},
    {L"braucht Widescreen", L"needs widescreen"},
    {L"Auto (an: Steam Deck)", L"Auto (on: Steam Deck)"},
    {L"Auto (aus)", L"Auto (off)"},
    {L"Kinobalken entfernen (experimentell)", L"Remove cinema bars (experimental)"},
    {L"HD-Texturen", L"HD textures"},
    {L"Scharfe Schrift", L"Sharp text"},
    {L"60 fps (experimentell)", L"60 fps (experimental)"},
    {L"60 fps auch in KÃ¤mpfen (experimentell)", L"60 fps in battles too (experimental)"},
    {L"Zwischensequenzen mit START Ã¼berspringen", L"Skip cutscenes with START"},
    {L"Filme mit START Ã¼berspringen", L"Skip movies with START"},
    {L"NachschÃ¤rfen", L"Sharpening"},
    {L"Bildfilter", L"Image filter"},
    {L"Weich", L"Smooth"},
    {L"Scharf-weich", L"Sharp-smooth"},
    {L"SeitenverhÃ¤ltnis", L"Aspect ratio"},
    {L"Fenster fÃ¼llen", L"Fill window"},
    {L"Bildschirm (Ultrawide)", L"Screen (ultrawide)"},
    {L"Vollbild", L"Fullscreen"},
    {L"Bildrate anzeigen", L"Show frame rate"},
    {L"Steuerung", L"Controls"},
    {L"Kamera umkehren (rechter Stick links/rechts)", L"Invert camera (right stick left/right)"},
    {L"Interne AuflÃ¶sung (ab Neustart)", L"Internal resolution (after restart)"},
    {L"Automatisch", L"Automatic"},
    {L"LautstÃ¤rke gesamt", L"Master volume"},
    {L"Musik", L"Music"},
    {L"Sprache", L"Voice"},
    {L"Effekte", L"Effects"},
    {L"Videos", L"Movies"},
    {L"Sprache (Language)", L"Language"},
    {L"Die Spieltexte wechseln beim nÃ¤chsten Start.", L"The game text changes at the next start."},
    {L"Disc wechseln (Neustart)", L"Change disc (restart)"},
    {L"Nochmal bestÃ¤tigen!", L"Confirm again!"},
    {L"Nicht gespeicherter Fortschritt geht verloren.", L"Unsaved progress will be lost."},
    {L"Savestates (Spielstand an jeder Stelle)", L"Savestates (save anywhere)"},
    {L"  (leer)", L"  (empty)"},
    {L"Savestate speichern (F5, Back+R1)", L"Save savestate (F5, Back+R1)"},
    {L"speichern", L"save"},
    {L"Savestate laden (F9, Back+L1)", L"Load savestate (F9, Back+L1)"},
    {L"laden", L"load"},
    {L"Tasten-/Controllerbelegung â€¦", L"Keyboard/controller mapping â€¦"},
    {L"Ã¶ffnen", L"open"},
    {L"ZurÃ¼ck zum Spiel", L"Back to the game"},
    {L"Spiel beenden", L"Quit game"},
    {L"beenden", L"quit"},
    {L"â†‘â†“ Auswahl    â†â†’ Ã¤ndern    Kreuz/Enter: auswÃ¤hlen    Kreis/Esc/F1: schlieÃŸen",
     L"â†‘â†“ select    â†â†’ change    Cross/Enter: choose    Circle/Esc/F1: close"},
    {L"Zwischensequenz wird Ã¼bersprungen â€¦", L"Skipping the cutscene â€¦"},
    {L"START: abbrechen", L"START: cancel"},
    // savestates (savestate.cpp)
    {L"Speichern lÃ¤uft noch â€¦", L"Still saving â€¦"},
    {L" ist leer", L" is empty"},
    {L"Speichern gerade nicht mÃ¶glich", L"Cannot save right now"},
    {L"Gespeichert in Slot ", L"Saved to slot "},
    {L"Speichern fehlgeschlagen", L"Saving failed"},
    {L"Laden fehlgeschlagen", L"Loading failed"},
    {L"Laden nicht mÃ¶glich", L"Cannot load"},
    {L"Der Spielstand gehÃ¶rt zu Disc ", L"The savestate belongs to disc "},
    {L"Savestate einer anderen Programmversion", L"Savestate of another program version"},
    {L"Savestate unvollstÃ¤ndig", L"Savestate incomplete"},
    {L" geladen", L" loaded"},
    {L"Zwischensequenz wird Ã¼bersprungen", L"a cutscene is being skipped"},
    {L"wÃ¤hrend eines Films nicht mÃ¶glich", L"not possible during a movie"},
    {L"Speicherkarte wird gerade benutzt", L"the memory card is in use"},
    {L"das Spiel ist noch nicht fertig gestartet", L"the game has not finished starting"},
    {L"Datei nicht gefunden", L"file not found"},
    {L"anderes Savestate-Format", L"different savestate format"},
    {L"Datei unvollstÃ¤ndig", L"file incomplete"},
    {L"Entpacken nicht mÃ¶glich", L"cannot decompress"},
    {L"Datei beschÃ¤digt", L"file damaged"},
    // first start: the player's disc image (main.cpp)
    {L"Grandia III (USA) Disc %d: Abbild deiner Disc wÃ¤hlen (ISO)", L"Grandia III (USA) disc %d: choose the image of your disc (ISO)"},
    {L"Grandia III braucht ein Abbild (ISO) deiner eigenen Disc 1 von Grandia III (USA). Bitte im nÃ¤chsten Fenster auswÃ¤hlen; es wird nicht verÃ¤ndert oder kopiert.",
     L"Digimon World 4 needs an image (ISO) of your own Digimon World 4 (USA) disc. Please choose it in the next window; it is neither changed nor copied."},
    {L"FÃ¼r Disc 2 braucht Grandia III das Abbild (ISO) deiner Disc 2 von Grandia III (USA). Bitte im nÃ¤chsten Fenster auswÃ¤hlen.",
     L"For disc 2 Digimon World 4 needs the image (ISO) of your disc 2. Please choose it in the next window."},
    {L"Das ist kein Abbild von Grandia III (USA) Disc %d. Bitte das passende auswÃ¤hlen.",
     L"This is not an image of Digimon World 4 (USA) disc %d. Please choose the right one."},
    // settings window (settings.cpp)
    {L"Digimon World 4  -  Einstellungen", L"Digimon World 4  -  Settings"},
    {L"Kreuz (\x2715)", L"Cross (\x2715)"},
    {L"Kreis (\x25CB)", L"Circle (\x25CB)"},
    {L"Quadrat (\x25A1)", L"Square (\x25A1)"},
    {L"Dreieck (\x25B3)", L"Triangle (\x25B3)"},
    {L"Steuerkreuz hoch", L"D-pad up"},
    {L"Steuerkreuz runter", L"D-pad down"},
    {L"Steuerkreuz links", L"D-pad left"},
    {L"Steuerkreuz rechts", L"D-pad right"},
    {L"L3 (linker Stick drÃ¼cken)", L"L3 (press left stick)"},
    {L"R3 (rechter Stick drÃ¼cken)", L"R3 (press right stick)"},
    {L"Rennen (gedrÃ¼ckt halten)", L"Run (hold)"},
    {L"Linker Stick drÃ¼cken", L"Press left stick"},
    {L"Rechter Stick drÃ¼cken", L"Press right stick"},
    {L"Stick ganz durchdrÃ¼cken", L"Push the stick all the way"},
    {L"Taste 0x%02X", L"Key 0x%02X"},
    {L"Bitte zuerst eine Zeile in der Liste auswÃ¤hlen.", L"Please select a row in the list first."},
    {L"Rennen geht mit dem Controller Ã¼ber den Analogstick.", L"With a controller you run by pushing the analog stick all the way."},
    {L"DrÃ¼cke jetzt die Taste fÃ¼r \"", L"Now press the key for \""},
    {L"DrÃ¼cke jetzt die Controller-Taste fÃ¼r \"", L"Now press the controller button for \""},
    {L"\"  (Esc = abbrechen)", L"\"  (Esc = cancel)"},
    {L"Abgebrochen.", L"Cancelled."},
    {L"Zugewiesen: ", L"Assigned: "},
    {L"Kein Controller gefunden (XInput, DualShock 4 oder DualSense).  Esc = abbrechen", L"No controller found (XInput, DualShock 4 or DualSense).  Esc = cancel"},
    {L"aus", L"off"},
    {L"Das Spiel wird jetzt mit Disc 2 neu gestartet.\nNicht gespeicherter Fortschritt geht verloren.",
     L"The game now restarts with disc 2.\nUnsaved progress will be lost."},
    {L"Das Spiel wird jetzt mit Disc 1 neu gestartet.\nNicht gespeicherter Fortschritt geht verloren.",
     L"The game now restarts with disc 1.\nUnsaved progress will be lost."},
    {L"Disc wechseln", L"Change disc"},
    {L"Renderer und interne AuflÃ¶sung werden beim nÃ¤chsten Start des Spiels Ã¼bernommen.",
     L"Renderer and internal resolution apply at the next start of the game."},
    {L"Interne AuflÃ¶sung:", L"Internal resolution:"},
    {L"GPU (hohe AuflÃ¶sung)", L"GPU (high resolution)"},
    {L"SeitenverhÃ¤ltnis:", L"Aspect ratio:"},
    {L"Bildfilter:", L"Image filter:"},
    {L"Pixel (scharf)", L"Pixel (sharp)"},
    {L"Bilinear (weich)", L"Bilinear (smooth)"},
    {L"Scharf-bilinear", L"Sharp bilinear"},
    {L"Widescreen (Taste F6)", L"Widescreen (key F6)"},
    {L"MenÃ¼s/Texte im 4:3-Format (Widescreen)", L"Menus/text in 4:3 format (widescreen)"},
    {L"Vollbild (Taste F11 / Alt+Enter)", L"Fullscreen (F11 / Alt+Enter)"},
    {L"VSync (kein Tearing)", L"VSync (no tearing)"},
    {L"Bildrate anzeigen (F7)", L"Show frame rate (F7)"},
    {L"HD-Texturen (Ordner \"textures\", Taste F8)", L"HD textures (folder \"textures\", key F8)"},
    {L"60 fps (experimentell, Taste F3)", L"60 fps (experimental, key F3)"},
    {L"NachschÃ¤rfen:", L"Sharpening:"},
    {L"Renderer/AuflÃ¶sung gelten ab nÃ¤chstem Start.", L"Renderer/resolution apply at the next start."},
    {L"Disc 2 (Neustart)", L"Disc 2 (restart)"},
    {L"Musik:", L"Music:"},
    {L"Sprache:", L"Voice:"},
    {L"Effekte:", L"Effects:"},
    {L"Videos:", L"Movies:"},
    {L"Gesamt:", L"Master:"},
    {L"Steuerung", L"Controls"},
    {L"PS2-Taste", L"PS2 button"},
    {L"Tastatur", L"Keyboard"},
    {L"Tastaturtaste Ã¤ndern", L"Change key"},
    {L"Controllertaste Ã¤ndern", L"Change button"},
    {L"Standardbelegung", L"Defaults"},
    {L"Zeile auswÃ¤hlen (Doppelklick = Tastaturtaste Ã¤ndern).", L"Select a row (double-click = change key)."},
    {L"Speichern", L"Save"},
    {L"Abbrechen", L"Cancel"},
    {L"Standardbelegung wiederhergestellt (wird mit \"Speichern\" Ã¼bernommen).", L"Default mapping restored (applied with \"Save\")."},
};

bool ui_german() { return config().language == "de"; }

const wchar_t* tr(const wchar_t* de) {
    if (!de || ui_german()) return de;
    static const std::unordered_map<std::wstring, const wchar_t*> map = [] {
        std::unordered_map<std::wstring, const wchar_t*> m;
        for (auto& t : TEXTS) m[t[0]] = t[1];
        return m;
    }();
    auto it = map.find(de);
    return it == map.end() ? de : it->second;
}

std::wstring tr(const std::wstring& de) { return tr(de.c_str()); }

// "en" and every lang\<code>.tsv (a two-letter code) next to the executable
std::vector<std::string> languages() {
    std::vector<std::string> v{"en"};
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((exe_dir() + "\\lang\\*.tsv").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n.size() == 6 && n.compare(2, 4, ".tsv") == 0) {
                std::string code = n.substr(0, 2);
                for (char& ch : code) ch = (char)tolower((unsigned char)ch);
                if (code != "en") v.push_back(code);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return v;
}

std::wstring language_name(const std::string& code) {
    if (code == "en") return L"English";
    if (code == "de") return L"Deutsch";
    if (code == "fr") return L"FranÃ§ais";
    if (code == "es") return L"EspaÃ±ol";
    if (code == "it") return L"Italiano";
    return std::wstring(code.begin(), code.end());
}

}  // namespace platform


