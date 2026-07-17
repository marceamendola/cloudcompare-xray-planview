#pragma once

#include <ccOverlayDialog.h>

class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class ccPointCloud;
class XRayViewport;

class XRayDialog : public ccOverlayDialog
{
	Q_OBJECT

public:
	explicit XRayDialog( ccPointCloud* cloud, QWidget* parent = nullptr );

private slots:
	void syncZMinFromSlider( int value );
	void syncZMaxFromSlider( int value );
	void applyControls();
	void resetZRange();

private:
	void buildUi();
	void initializeFromCloud();
	double sliderToZ( int value ) const;
	int zToSlider( double value ) const;

	ccPointCloud* m_cloud = nullptr;
	XRayViewport* m_viewport = nullptr;
	QLabel* m_infoLabel = nullptr;
	QDoubleSpinBox* m_zMinSpin = nullptr;
	QDoubleSpinBox* m_zMaxSpin = nullptr;
	QDoubleSpinBox* m_gammaSpin = nullptr;
	QSlider* m_zMinSlider = nullptr;
	QSlider* m_zMaxSlider = nullptr;
	QCheckBox* m_invertCheck = nullptr;
	QPushButton* m_allZButton = nullptr;
	QPushButton* m_closeButton = nullptr;

	double m_cloudZMin = 0.0;
	double m_cloudZMax = 0.0;
};
