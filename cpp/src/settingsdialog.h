// File > Settings: the few things worth changing, with their limits shown.

#pragma once

#include <QDialog>

#include "settings.h"

class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const Preferences &current, QWidget *parent = nullptr);

    /// What the dialog holds now, whether or not it was accepted.
    Preferences preferences() const;

    // For the tests, which set values the way a user would.
    QSpinBox *cacheBox() const { return m_cache; }
    QSpinBox *prefetchBox() const { return m_prefetch; }
    QSpinBox *slideshowBox() const { return m_slideshow; }

private:
    QSpinBox *m_cache;
    QSpinBox *m_prefetch;
    QSpinBox *m_slideshow;
};
