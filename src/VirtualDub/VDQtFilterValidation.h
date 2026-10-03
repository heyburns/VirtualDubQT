#ifndef VDQTFILTERVALIDATION_H
#define VDQTFILTERVALIDATION_H

#include "VDQtFilterSystem.h"
#include <QSize>

// One numeric contract for JSON, scripts, generic controls and the processing
// boundary. Missing keys keep their established defaults; unknown extension
// keys remain compatible but cannot contain non-finite/unbounded numbers.
struct VDQtFilterParameterSpec {
    double minimum = -1000000;
    double maximum = 1000000;
    bool integer = false;
};
VDQtFilterParameterSpec VDQtFilterParameter(VDFilterType type, const QString& key);
bool VDQtValidateFilter(const VDFilterInstance& filter, QString *error = nullptr,
                        QSize inputSize = {}, int bytesPerPixel = 8);

#endif
