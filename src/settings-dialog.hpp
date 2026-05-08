#pragma once

#include "tod-timer-source.hpp" // for obs_data_t + S_* defines

#include <QDialog>
#include <QFont>
#include <QColor>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QFontComboBox;
class QSpinBox;

// --------------------------------------------------------------------------
// SettingsDialog
//
// Full-featured Qt dialog for editing all TOD Timer source settings.
// Opened via the "Configure…" button in the OBS properties panel.
//
// Usage:
//   obs_data_t *s = obs_source_get_settings(source);
//   SettingsDialog dlg(s, parent);
//   if (dlg.exec() == QDialog::Accepted)
//       dlg.applyToSettings(s);
//   obs_data_release(s);
// --------------------------------------------------------------------------
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(obs_data_t *settings, QWidget *parent = nullptr);

	// Write the current dialog state back into an obs_data_t.
	// Call this only after exec() returns Accepted.
	void applyToSettings(obs_data_t *settings) const;

private slots:
	void onChooseColor();
	void onFontFamilyChanged(const QFont &font);

private:
	// ── Target time ──────────────────────────────────────────────────────────
	QLineEdit *m_timeEdit{nullptr}; // free-form "h:mm:ss.t"
	QComboBox *m_ampm{nullptr};     // AM / PM

	// ── Display format (ProPresenter-style blue pill combos) ─────────────────
	QComboBox *m_fmtHours{nullptr};
	QComboBox *m_fmtMinutes{nullptr};
	QComboBox *m_fmtSeconds{nullptr};
	QComboBox *m_fmtTenths{nullptr};

	// ── Appearance ───────────────────────────────────────────────────────────
	QFontComboBox *m_fontFamily{nullptr}; // font family picker
	QComboBox *m_fontStyle{nullptr};      // style within family (Regular, Bold, Heavy…)
	QSpinBox *m_fontSize{nullptr};        // point size
	QPushButton *m_colorButton{nullptr};  // colored swatch; opens QColorDialog
	QCheckBox *m_shadowCheck{nullptr};
	QCheckBox *m_outlineCheck{nullptr};

	// ── Behaviour ────────────────────────────────────────────────────────────
	QCheckBox *m_autoStart{nullptr};
	QCheckBox *m_autoStop{nullptr};
	QCheckBox *m_stopAtZero{nullptr};
	QCheckBox *m_hideAtZero{nullptr};

	// Internal state for color (kept so we can re-open dialog
	// pre-seeded with the current selection).
	QColor m_color;

	// Build the widget tree (called once from constructor).
	void buildUi();

	// Populate all widgets from an obs_data_t snapshot (called after buildUi).
	void populateFromSettings(obs_data_t *settings);

	// Create one format-picker QComboBox styled as a blue pill.
	// abbrev is the single-letter abbreviation for that segment (h/m/s).
	// Items store their SegmentFormat int value as QVariant item data.
	QComboBox *createFormatCombo(const QString &abbrev, QWidget *parent);

	// Simplified variant for tenths (single digit 0-9 — leading-zero options
	// are meaningless, and there is no unit below to roll into).
	QComboBox *createTenthsFormatCombo(QWidget *parent);

	// Refresh color swatch button after a color selection.
	void updateColorButton();
	// Repopulate m_fontStyle from QFontDatabase for the given family.
	void updateStyleCombo(const QString &family, const QString &currentStyle = {});

	// Parse m_timeEdit text + m_ampm selection → 24-hour components.
	// Handles partial input: "1:30" → 1 h, 30 min, 0 s, 0 t.
	void parseTimeInput(int &hour, int &min, int &sec, int &tenths) const;
};
