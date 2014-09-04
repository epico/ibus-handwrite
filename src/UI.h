

#ifndef UI_H_
#define UI_H_

#include "engine.h"


void UI_buildui(IBusHandwriteEngine * engine);
void UI_show_ui(IBusHandwriteEngine * engine);
void UI_hide_ui(IBusHandwriteEngine * engine);
void UI_cancelui(IBusHandwriteEngine* engine); //Cancel
void regen_loopuptable(GtkWidget * widget, IBusHandwriteEngine * engine);

#define INITIAL_WIDTH 390
#define PANELHEIGHT 90

#endif /* UI_H_ */
