// Content packs, as a section of the gear's page: import a homebrew folder or .zip, switch a pack on or off, remove one, and
// what each pack brought (counts, warnings). The Core pack is always on. The work itself is the core's (PackManager); this only
// asks for the file, shows the result and reloads the content.
#pragma once

#include "parsing/packs.h"
#include "ui/filedialog.h"
#include "ui/module.h"

namespace gm {

class PacksPanel {
public:
    explicit PacksPanel(Host& host) : host_(host) {}

    bool busy() const { return dialog_.busy(); }    // a file dialog is open: keep the frames coming
    void update();                                  // once per frame: takes the file the dialog returned and imports it
    void draw();

private:
    Host& host_;
    FileDialog dialog_;
    ImportResult result_;
    bool haveResult_ = false;
};

}  // namespace gm
