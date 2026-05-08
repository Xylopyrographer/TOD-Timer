#include "settings-dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QSpinBox>
#include <QColorDialog>
#include <QString>

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
// Return all style names for a font family via Core Text, which enumerates
// the full set of faces that macOS Font Book shows (Qt only returns ~20).
static QStringList coreTextStyles( const QString &family ) {
    CFStringRef familyCF = CFStringCreateWithCharacters(
                               kCFAllocatorDefault,
                               reinterpret_cast<const UniChar *>( family.constData() ),
                               static_cast<CFIndex>( family.size() ) );
    CFDictionaryRef attrs = CFDictionaryCreate(
                                kCFAllocatorDefault,
                                ( const void ** )&kCTFontFamilyNameAttribute,
                                ( const void ** )&familyCF,
                                1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );
    CTFontDescriptorRef proto = CTFontDescriptorCreateWithAttributes( attrs );
    CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors( proto, nullptr );
    CFRelease( proto );
    CFRelease( attrs );
    CFRelease( familyCF );

    QStringList styles;
    if ( matches ) {
        const CFIndex n = CFArrayGetCount( matches );
        for ( CFIndex i = 0; i < n; ++i ) {
            auto *desc = ( CTFontDescriptorRef )CFArrayGetValueAtIndex( matches, i );
            auto *style = ( CFStringRef )CTFontDescriptorCopyAttribute( desc, kCTFontStyleNameAttribute );
            if ( style ) {
                const CFIndex len = CFStringGetLength( style );
                QString s( static_cast<int>( len ), Qt::Uninitialized );
                CFStringGetCharacters( style, CFRangeMake( 0, len ),
                                       reinterpret_cast<UniChar *>( s.data() ) );
                CFRelease( style );
                if ( !s.isEmpty() && !styles.contains( s ) ) {
                    styles << s;
                }
            }
        }
        CFRelease( matches );
    }
    styles.sort( Qt::CaseInsensitive );
    return styles;
}
#endif

// --------------------------------------------------------------------------
// Stylesheet for the format-picker "blue pill" QComboBox
// --------------------------------------------------------------------------
static const char *FMT_PILL_STYLE = R"(
QComboBox {
    background-color: #1a73e8;
    color: white;
    border: none;
    border-radius: 5px;
    padding: 4px 4px 4px 10px;
    font-weight: bold;
    font-size: 13px;
    min-width: 52px;
}
QComboBox:hover {
    background-color: #1557b0;
}
QComboBox::drop-down {
    border: none;
    width: 18px;
    background: transparent;
}
QComboBox QAbstractItemView {
    background-color: #2d2d2d;
    color: white;
    selection-background-color: #1a73e8;
    border: 1px solid #555;
    outline: none;
    padding: 2px;
    font-size: 15px;
}
)";

// Large bold separator label between the format pickers (: and .)
static const char *SEP_STYLE = "font-size: 18px; font-weight: bold; padding: 0 3px;";

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

SettingsDialog::SettingsDialog( obs_data_t *settings, QWidget *parent )
    : QDialog( parent ) {
    setWindowTitle( obs_module_text( "SettingsTitle" ) );
    setMinimumWidth( 400 );
    buildUi();
    populateFromSettings( settings );
}

// --------------------------------------------------------------------------
// UI construction
// --------------------------------------------------------------------------

void SettingsDialog::buildUi() {
    auto *mainLayout = new QVBoxLayout( this );
    mainLayout->setSpacing( 10 );

    // ── Target Time ──────────────────────────────────────────────────────────
    auto *timeGroup  = new QGroupBox( obs_module_text( "TargetTimeGroup" ), this );
    auto *timeLayout = new QHBoxLayout( timeGroup );

    m_timeEdit = new QLineEdit( timeGroup );
    m_timeEdit->setPlaceholderText( "h:mm:ss.t" );
    m_timeEdit->setMaximumWidth( 150 );
    m_timeEdit->setToolTip(
        obs_module_text( "TargetTimeToolTip" ) );

    m_ampm = new QComboBox( timeGroup );
    m_ampm->addItem( "AM" );
    m_ampm->addItem( "PM" );

    timeLayout->addWidget( new QLabel( obs_module_text( "TargetTimeLabel" ), timeGroup ) );
    timeLayout->addWidget( m_timeEdit );
    timeLayout->addWidget( m_ampm );
    timeLayout->addStretch();

    // ── Display Format ───────────────────────────────────────────────────────
    // Four blue-pill combos: [h ▾] : [mm ▾] : [ss ▾] . [t ▾]
    auto *fmtGroup  = new QGroupBox( obs_module_text( "DisplayFormatGroup" ), this );
    auto *fmtLayout = new QHBoxLayout( fmtGroup );
    fmtLayout->setSpacing( 4 );

    m_fmtHours   = createFormatCombo( "h", fmtGroup );
    m_fmtMinutes = createFormatCombo( "m", fmtGroup );
    m_fmtSeconds = createFormatCombo( "s", fmtGroup );
    m_fmtTenths  = createTenthsFormatCombo( fmtGroup );

    auto *sep1 = new QLabel( ":", fmtGroup );
    sep1->setStyleSheet( SEP_STYLE );
    auto *sep2 = new QLabel( ":", fmtGroup );
    sep2->setStyleSheet( SEP_STYLE );
    auto *sep3 = new QLabel( ".", fmtGroup );
    sep3->setStyleSheet( SEP_STYLE );

    fmtLayout->addWidget( m_fmtHours );
    fmtLayout->addWidget( sep1 );
    fmtLayout->addWidget( m_fmtMinutes );
    fmtLayout->addWidget( sep2 );
    fmtLayout->addWidget( m_fmtSeconds );
    fmtLayout->addWidget( sep3 );
    fmtLayout->addWidget( m_fmtTenths );
    fmtLayout->addStretch();

    // ── Appearance ───────────────────────────────────────────────────────────
    auto *appearGroup  = new QGroupBox( obs_module_text( "AppearanceGroup" ), this );
    auto *appearLayout = new QFormLayout( appearGroup );
    appearLayout->setRowWrapPolicy( QFormLayout::DontWrapRows );

    m_fontFamily = new QFontComboBox( appearGroup );
    m_fontStyle  = new QComboBox( appearGroup );
    m_fontStyle->setMinimumWidth( 100 );
    m_fontStyle->setMaxVisibleItems( 40 );
    connect( m_fontFamily, &QFontComboBox::currentFontChanged,
             this, &SettingsDialog::onFontFamilyChanged );
    m_fontSize = new QSpinBox( appearGroup );
    m_fontSize->setRange( 6, 288 );
    m_fontSize->setSuffix( " pt" );
    auto *fontRow = new QHBoxLayout;
    fontRow->setSpacing( 6 );
    fontRow->addWidget( m_fontFamily, 1 );
    fontRow->addWidget( m_fontStyle );
    fontRow->addWidget( m_fontSize );
    appearLayout->addRow( obs_module_text( "Font" ), fontRow );

    m_colorButton = new QPushButton( appearGroup );
    m_colorButton->setFixedHeight( 24 );
    connect( m_colorButton, &QPushButton::clicked, this, &SettingsDialog::onChooseColor );
    m_shadowCheck  = new QCheckBox( obs_module_text( "Shadow" ),  appearGroup );
    m_outlineCheck = new QCheckBox( obs_module_text( "Stroke" ),  appearGroup );
    auto *checkCol = new QVBoxLayout;
    checkCol->setSpacing( 2 );
    checkCol->addWidget( m_shadowCheck );
    checkCol->addWidget( m_outlineCheck );
    auto *colorRow = new QHBoxLayout;
    colorRow->setSpacing( 8 );
    colorRow->addWidget( m_colorButton );
    colorRow->addLayout( checkCol );
    colorRow->addStretch();
    appearLayout->addRow( obs_module_text( "Color" ), colorRow );

    // ── Behaviour ────────────────────────────────────────────────────────────
    auto *behavGroup  = new QGroupBox( obs_module_text( "BehaviourGroup" ), this );
    auto *behavLayout = new QVBoxLayout( behavGroup );

    m_autoStart  = new QCheckBox( obs_module_text( "AutoStart" ),  behavGroup );
    m_autoStop   = new QCheckBox( obs_module_text( "AutoStop" ),   behavGroup );
    m_stopAtZero = new QCheckBox( obs_module_text( "StopAtZero" ), behavGroup );
    m_hideAtZero = new QCheckBox( obs_module_text( "HideAtZero" ), behavGroup );

    behavLayout->addWidget( m_autoStart );
    behavLayout->addWidget( m_autoStop );
    behavLayout->addWidget( m_stopAtZero );
    behavLayout->addWidget( m_hideAtZero );

    // ── Dialog buttons ───────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this );
    connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    mainLayout->addWidget( timeGroup );
    mainLayout->addWidget( fmtGroup );
    mainLayout->addWidget( appearGroup );
    mainLayout->addWidget( behavGroup );
    mainLayout->addStretch();
    mainLayout->addWidget( buttons );
}

// --------------------------------------------------------------------------
// Format-picker combo factory
// --------------------------------------------------------------------------

// Apply combining long stroke overlay (U+0336) after every character in s,
// producing a fully struck-through string when rendered.
static QString struck( const QString &s ) {
    const QChar k( 0x0336 );
    QString r;
    r.reserve( s.size() * 2 );
    for ( QChar c : s ) {
        r += c;
        r += k;
    }
    return r;
}

QComboBox *SettingsDialog::createFormatCombo( const QString &abbrev, QWidget *parent ) {
    auto *cb = new QComboBox( parent );
    cb->setStyleSheet( FMT_PILL_STYLE );

    // Conditional items use ─X─ / ─XX─ with every character struck-through
    // (U+0336), so the strikethrough extends continuously across the dashes
    // and the letter(s), making it clearly readable.
    // Each item stores its SegmentFormat int value as QVariant item data.
    const QChar dash( 0x2500 );                   // ─  BOX DRAWINGS LIGHT HORIZONTAL
    const QString sk  = struck( dash + abbrev + dash );       // e.g. ─̶h̶─̶
    const QString skk = struck( dash + abbrev + abbrev + dash ); // e.g. ─̶h̶h̶─̶

    cb->addItem( abbrev,          QVariant( 0 ) ); // ALWAYS_NO_LEAD  — h / m / s
    cb->addItem( abbrev + abbrev, QVariant( 1 ) ); // ALWAYS_LEAD     — hh / mm / ss
    cb->addItem( sk,              QVariant( 2 ) ); // IF_NONZERO_NO_LEAD
    cb->addItem( skk,             QVariant( 3 ) ); // IF_NONZERO_LEAD
    cb->addItem( "--",            QVariant( 4 ) ); // HIDE_ROLLDOWN
    return cb;
}

QComboBox *SettingsDialog::createTenthsFormatCombo( QWidget *parent ) {
    auto *cb = new QComboBox( parent );
    cb->setStyleSheet( FMT_PILL_STYLE );

    // Tenths is always a single digit (0–9): only "always show" or "hide".
    cb->addItem( "t",  QVariant( 0 ) ); // ALWAYS_NO_LEAD
    cb->addItem( "--", QVariant( 4 ) ); // HIDE_ROLLDOWN
    return cb;
}

// --------------------------------------------------------------------------
// Populate from obs_data_t
// --------------------------------------------------------------------------

void SettingsDialog::populateFromSettings( obs_data_t *settings ) {
    // ── Target time ──────────────────────────────────────────────────────────
    const int h24 = ( int )obs_data_get_int( settings, S_TARGET_HOUR );
    const int min = ( int )obs_data_get_int( settings, S_TARGET_MINUTE );
    const int sec = ( int )obs_data_get_int( settings, S_TARGET_SECOND );
    const int tth = ( int )obs_data_get_int( settings, S_TARGET_TENTHS );

    // Convert stored 24-hour value to 12-hour display + AM/PM.
    int  displayH;
    bool isPm;
    if      ( h24 == 0 )  {
        displayH = 12;    // midnight
        isPm = false;
    }
    else if ( h24 < 12 )  {
        displayH = h24;    // 1–11 AM
        isPm = false;
    }
    else if ( h24 == 12 ) {
        displayH = 12;     // noon
        isPm = true;
    }
    else                {
        displayH = h24 - 12;    // 1–11 PM
        isPm = true;
    }

    m_timeEdit->setText(
        QString( "%1:%2:%3.%4" )
        .arg( displayH )
        .arg( min, 2, 10, QChar( '0' ) )
        .arg( sec, 2, 10, QChar( '0' ) )
        .arg( tth ) );
    m_ampm->setCurrentIndex( isPm ? 1 : 0 );

    // ── Display format ───────────────────────────────────────────────────────
    // Use findData() so the combo works regardless of item order / subset.
    auto setFmt = []( QComboBox * cb, int val ) {
        const int idx = cb->findData( QVariant( val ) );
        cb->setCurrentIndex( idx >= 0 ? idx : 0 );
    };
    setFmt( m_fmtHours,   ( int )obs_data_get_int( settings, S_FMT_HOURS ) );
    setFmt( m_fmtMinutes, ( int )obs_data_get_int( settings, S_FMT_MINUTES ) );
    setFmt( m_fmtSeconds, ( int )obs_data_get_int( settings, S_FMT_SECONDS ) );
    setFmt( m_fmtTenths,  ( int )obs_data_get_int( settings, S_FMT_TENTHS ) );

    // ── Font ─────────────────────────────────────────────────────────────────
    obs_data_t *fontObj = obs_data_get_obj( settings, S_FONT );
    if ( fontObj ) {
        const QString face  = QString::fromUtf8( obs_data_get_string( fontObj, "face" ) );
        const QString style = QString::fromUtf8( obs_data_get_string( fontObj, "style" ) );
        const int     size  = ( int )obs_data_get_int( fontObj, "size" );
        m_fontFamily->setCurrentFont( QFont( face ) );
        updateStyleCombo( face, style );
        m_fontSize->setValue( size > 0 ? size : 72 );
        obs_data_release( fontObj );
    }
    else {
        updateStyleCombo( m_fontFamily->currentFont().family() );
    }

    // ── Color ────────────────────────────────────────────────────────────────
    // OBS stores color as ABGR: bits 0–7 = R, 8–15 = G, 16–23 = B, 24–31 = A.
    const auto abgr = ( uint32_t )obs_data_get_int( settings, S_COLOR );
    m_color = QColor(
                  ( int )( ( abgr >>  0 ) & 0xFF ), // R
                  ( int )( ( abgr >>  8 ) & 0xFF ), // G
                  ( int )( ( abgr >> 16 ) & 0xFF ), // B
                  ( int )( ( abgr >> 24 ) & 0xFF ) ); // A
    updateColorButton();

    // ── Appearance ───────────────────────────────────────────────────────────
    m_shadowCheck->setChecked( obs_data_get_bool( settings, S_DROP_SHADOW ) );
    m_outlineCheck->setChecked( obs_data_get_bool( settings, S_OUTLINE ) );

    // ── Behaviour ────────────────────────────────────────────────────────────
    m_autoStart->setChecked( obs_data_get_bool( settings, S_AUTO_START ) );
    m_autoStop->setChecked(  obs_data_get_bool( settings, S_AUTO_STOP ) );
    m_stopAtZero->setChecked( obs_data_get_bool( settings, S_STOP_AT_ZERO ) );
    m_hideAtZero->setChecked( obs_data_get_bool( settings, S_HIDE_AT_ZERO ) );
}

// --------------------------------------------------------------------------
// Button label helpers
// --------------------------------------------------------------------------

void SettingsDialog::updateColorButton() {
    // Fill the button with the selected color so it acts as a swatch.
    m_colorButton->setStyleSheet(
        QString( "background-color: rgba(%1,%2,%3,%4); border: 1px solid #888;" )
        .arg( m_color.red() )
        .arg( m_color.green() )
        .arg( m_color.blue() )
        .arg( m_color.alpha() ) );
    m_colorButton->setText( "" );
}

// --------------------------------------------------------------------------
// Font / color slots
// --------------------------------------------------------------------------

void SettingsDialog::onChooseColor() {
    QColor c = QColorDialog::getColor(
                   m_color, this,
                   obs_module_text( "ChooseColor" ),
                   QColorDialog::ShowAlphaChannel );
    if ( c.isValid() ) {
        m_color = c;
        updateColorButton();
    }
}

void SettingsDialog::onFontFamilyChanged( const QFont &font ) {
    updateStyleCombo( font.family() );
}

void SettingsDialog::updateStyleCombo( const QString &family, const QString &currentStyle ) {
    const QString restore = currentStyle.isEmpty() ? m_fontStyle->currentText() : currentStyle;
    m_fontStyle->blockSignals( true );
    m_fontStyle->clear();
    #ifdef __APPLE__
    const QStringList styles = coreTextStyles( family );
    #else
    const QStringList styles = QFontDatabase::styles( family );
    #endif
    for ( const QString &s : styles ) {
        m_fontStyle->addItem( s );
    }
    const int idx = m_fontStyle->findText( restore );
    m_fontStyle->setCurrentIndex( idx >= 0 ? idx : 0 );
    m_fontStyle->blockSignals( false );
}

// --------------------------------------------------------------------------
// Time parsing
// --------------------------------------------------------------------------

void SettingsDialog::parseTimeInput( int &hour, int &min, int &sec, int &tenths ) const {
    hour = min = sec = tenths = 0;

    QString text = m_timeEdit->text().trimmed();
    if ( text.isEmpty() ) {
        return;
    }

    // Extract the tenths digit after the first "."
    const int dotIdx = text.indexOf( '.' );
    if ( dotIdx >= 0 ) {
        const QString tStr = text.mid( dotIdx + 1 ).trimmed();
        if ( !tStr.isEmpty() ) {
            const int d = tStr.at( 0 ).digitValue();
            tenths = ( d >= 0 ) ? qBound( 0, d, 9 ) : 0;
        }
        text = text.left( dotIdx );
    }

    // Parse h, h:mm, or h:mm:ss
    const QStringList parts = text.split( ':' );
    int displayH = 12;
    switch ( parts.size() ) {
        default:
        case 3:
            sec      = qBound( 0, parts[ 2 ].trimmed().toInt(), 59 );
            [ [ fallthrough ] ];
        case 2:
            min      = qBound( 0, parts[ 1 ].trimmed().toInt(), 59 );
            [ [ fallthrough ] ];
        case 1:
            displayH = parts[ 0 ].trimmed().toInt();
            break;
        case 0:
            break;
    }
    displayH = qBound( 1, displayH, 12 );

    // Convert 12h + AM/PM → 24h
    const bool isPm = ( m_ampm->currentIndex() == 1 );
    hour = isPm ? ( displayH == 12 ? 12 : displayH + 12 )
           : ( displayH == 12 ?  0 : displayH );
    hour = qBound( 0, hour, 23 );
}

// --------------------------------------------------------------------------
// Write back to obs_data_t
// --------------------------------------------------------------------------

void SettingsDialog::applyToSettings( obs_data_t *settings ) const {
    // ── Target time ──────────────────────────────────────────────────────────
    int hour, min, sec, tenths;
    parseTimeInput( hour, min, sec, tenths );
    obs_data_set_int( settings, S_TARGET_HOUR,   hour );
    obs_data_set_int( settings, S_TARGET_MINUTE, min );
    obs_data_set_int( settings, S_TARGET_SECOND, sec );
    obs_data_set_int( settings, S_TARGET_TENTHS, tenths );

    // ── Display format ───────────────────────────────────────────────────────
    obs_data_set_int( settings, S_FMT_HOURS,   m_fmtHours->currentData().toInt() );
    obs_data_set_int( settings, S_FMT_MINUTES, m_fmtMinutes->currentData().toInt() );
    obs_data_set_int( settings, S_FMT_SECONDS, m_fmtSeconds->currentData().toInt() );
    obs_data_set_int( settings, S_FMT_TENTHS,  m_fmtTenths->currentData().toInt() );

    // ── Font ─────────────────────────────────────────────────────────────────
    obs_data_t *fontObj = obs_data_create();
    const QString family = m_fontFamily->currentFont().family();
    const QString style  = m_fontStyle->currentText();
    obs_data_set_string( fontObj, "face",  family.toUtf8().constData() );
    obs_data_set_string( fontObj, "style", style.toUtf8().constData() );
    obs_data_set_int( fontObj,    "size",  m_fontSize->value() );
    // Derive bold/italic flags from the style name; QFontDatabase::bold/italic
    // may not recognise Core Text-only style names like "Heavy" or "Semibold".
    const QString styleLower = style.toLower();
    int flags = 0;
    if ( styleLower.contains( "bold" ) || styleLower.contains( "heavy" ) ||
            styleLower.contains( "black" ) || styleLower.contains( "semibold" ) ||
            styleLower.contains( "demibold" ) || styleLower.contains( "medium" ) ) {
        flags |= 1;
    }
    if ( styleLower.contains( "italic" ) || styleLower.contains( "oblique" ) ) {
        flags |= 2;
    }
    obs_data_set_int( fontObj, "flags", flags );
    obs_data_set_obj( settings, S_FONT, fontObj );
    obs_data_release( fontObj );

    // ── Color (ABGR: bits 0–7=R, 8–15=G, 16–23=B, 24–31=A) ─────────────────
    const uint32_t abgr =
        ( ( uint32_t )m_color.alpha() << 24 )
        | ( ( uint32_t )m_color.blue()  << 16 )
        | ( ( uint32_t )m_color.green() <<  8 )
        | ( ( uint32_t )m_color.red()   <<  0 );
    obs_data_set_int( settings, S_COLOR, ( int64_t )abgr );

    // ── Appearance ───────────────────────────────────────────────────────────
    obs_data_set_bool( settings, S_DROP_SHADOW, m_shadowCheck->isChecked() );
    obs_data_set_bool( settings, S_OUTLINE,     m_outlineCheck->isChecked() );

    // ── Behaviour ────────────────────────────────────────────────────────────
    obs_data_set_bool( settings, S_AUTO_START,   m_autoStart->isChecked() );
    obs_data_set_bool( settings, S_AUTO_STOP,    m_autoStop->isChecked() );
    obs_data_set_bool( settings, S_STOP_AT_ZERO, m_stopAtZero->isChecked() );
    obs_data_set_bool( settings, S_HIDE_AT_ZERO, m_hideAtZero->isChecked() );
}
