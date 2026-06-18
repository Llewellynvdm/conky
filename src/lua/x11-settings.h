#ifndef CONKY_X11_SETTINGS_H
#define CONKY_X11_SETTINGS_H

#include "setting.hh"

// defined in x11.cc

extern conky::simple_config_setting<std::string> display_name;

extern conky::simple_config_setting<bool> out_to_x;

#ifdef BUILD_XFT
extern conky::simple_config_setting<bool> use_xft;
#endif

#ifdef BUILD_XDBE
extern conky::simple_config_setting<bool> use_xdbe;
#else
extern conky::simple_config_setting<bool> use_xpmdb;
#endif

#endif /* CONKY_X11_SETTINGS_H */
