#pragma once

#include <QStringList>

namespace DeskPortCli {
// Both functions receive QCoreApplication::arguments(), including argv[0].
bool isSetupCommand(const QStringList& arguments);
int runSetupCommand(const QStringList& arguments);
}
