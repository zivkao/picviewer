#include "settingsdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const Preferences &current, QWidget *parent)
    : QDialog(parent)
    , m_cache(new QSpinBox(this))
    , m_prefetch(new QSpinBox(this))
    , m_slideshow(new QSpinBox(this))
{
    setWindowTitle(tr("Settings"));

    m_cache->setRange(Preferences::MinCacheMb, Preferences::MaxCacheMb);
    m_cache->setSingleStep(256);
    m_cache->setSuffix(tr(" MB"));
    m_cache->setValue(current.cacheMb);
    m_cache->setToolTip(tr("Decoded images kept in memory, so stepping back is instant.\n"
                           "A 24-megapixel photograph takes about 100 MB."));

    m_prefetch->setRange(Preferences::MinPrefetch, Preferences::MaxPrefetch);
    m_prefetch->setValue(current.prefetchRadius);
    m_prefetch->setToolTip(tr("Images decoded ahead on each side of the current one.\n"
                              "More makes stepping faster and uses more memory and CPU."));

    m_slideshow->setRange(Preferences::MinSlideshowSeconds, Preferences::MaxSlideshowSeconds);
    m_slideshow->setSuffix(tr(" s"));
    m_slideshow->setValue(current.slideshowSeconds);

    auto *form = new QFormLayout;
    form->addRow(tr("Memory cache:"), m_cache);
    form->addRow(tr("Decode ahead:"), m_prefetch);
    form->addRow(tr("Slideshow interval:"), m_slideshow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

Preferences SettingsDialog::preferences() const
{
    Preferences p;
    p.cacheMb = m_cache->value();
    p.prefetchRadius = m_prefetch->value();
    p.slideshowSeconds = m_slideshow->value();
    return p;
}
