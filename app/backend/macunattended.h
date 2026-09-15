#pragma once
#include <QString>
// Mirrors SMAppServiceStatus without exposing Objective-C to portable code.
int deskPortUnattendedServiceStatus();
bool deskPortSetUnattendedService(bool enabled, QString& error);
void deskPortOpenBackgroundItems();
