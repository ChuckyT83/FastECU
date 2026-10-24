#pragma once
#include <QString>
#include <QStringList>
namespace MockJ2534 {
extern QString fail;
extern QStringList calls;
extern int metadata;
extern long errorTextStatus;
extern bool loaded;
extern unsigned long device;
void reset();
}
