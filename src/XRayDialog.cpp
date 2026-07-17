#include "../include/XRayDialog.h"
#include "../include/XRayViewport.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <ccPointCloud.h>

#include <algorithm>

namespace
{
	constexpr int SliderMax = 10000;
}

XRayDialog::XRayDialog( ccPointCloud* cloud, QWidget* parent )
	: ccOverlayDialog( parent )
	, m_cloud( cloud )
{
	buildUi();
	initializeFromCloud();
	applyControls();
}

void XRayDialog::buildUi()
{
	setWindowTitle( "X-Ray Plan View" );
	resize( 980, 720 );

	m_viewport = new XRayViewport( this );
	m_infoLabel = new QLabel( this );

	m_zMinSpin = new QDoubleSpinBox( this );
	m_zMaxSpin = new QDoubleSpinBox( this );
	m_gammaSpin = new QDoubleSpinBox( this );
	m_zMinSlider = new QSlider( Qt::Horizontal, this );
	m_zMaxSlider = new QSlider( Qt::Horizontal, this );
	m_invertCheck = new QCheckBox( "Invert colors", this );
	m_allZButton = new QPushButton( "All Z", this );
	m_closeButton = new QPushButton( "Close", this );

	m_zMinSlider->setRange( 0, SliderMax );
	m_zMaxSlider->setRange( 0, SliderMax );
	m_gammaSpin->setRange( 0.15, 3.0 );
	m_gammaSpin->setSingleStep( 0.05 );
	m_gammaSpin->setValue( 0.75 );

	QGridLayout* controls = new QGridLayout();
	controls->addWidget( new QLabel( "Z min", this ), 0, 0 );
	controls->addWidget( m_zMinSpin, 0, 1 );
	controls->addWidget( m_zMinSlider, 0, 2 );
	controls->addWidget( new QLabel( "Z max", this ), 1, 0 );
	controls->addWidget( m_zMaxSpin, 1, 1 );
	controls->addWidget( m_zMaxSlider, 1, 2 );
	controls->addWidget( new QLabel( "Gamma", this ), 2, 0 );
	controls->addWidget( m_gammaSpin, 2, 1 );
	controls->addWidget( m_invertCheck, 2, 2 );
	controls->addWidget( m_allZButton, 2, 3 );

	QHBoxLayout* header = new QHBoxLayout();
	QLabel* title = new QLabel( "X-Ray Plan View", this );
	QFont titleFont = title->font();
	titleFont.setBold( true );
	title->setFont( titleFont );
	header->addWidget( title );
	header->addStretch( 1 );
	header->addWidget( m_closeButton );

	QVBoxLayout* layout = new QVBoxLayout( this );
	layout->addLayout( header );
	layout->addWidget( m_infoLabel );
	layout->addLayout( controls );
	layout->addWidget( m_viewport, 1 );

	connect( m_zMinSlider, &QSlider::valueChanged, this, &XRayDialog::syncZMinFromSlider );
	connect( m_zMaxSlider, &QSlider::valueChanged, this, &XRayDialog::syncZMaxFromSlider );
	connect( m_zMinSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, &XRayDialog::applyControls );
	connect( m_zMaxSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, &XRayDialog::applyControls );
	connect( m_gammaSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, &XRayDialog::applyControls );
	connect( m_invertCheck, &QCheckBox::toggled, this, &XRayDialog::applyControls );
	connect( m_allZButton, &QPushButton::clicked, this, &XRayDialog::resetZRange );
	connect( m_closeButton, &QPushButton::clicked, this, [this]()
	{
		stop( true );
	} );
}

void XRayDialog::initializeFromCloud()
{
	if ( !m_cloud || m_cloud->size() == 0 )
	{
		return;
	}

	const CCVector3* p0 = m_cloud->getPointPersistentPtr( 0 );
	m_cloudZMin = p0->z;
	m_cloudZMax = p0->z;

	const unsigned pointCount = m_cloud->size();
	const unsigned stride = pointCount > 5000000 ? pointCount / 5000000 : 1;
	for ( unsigned i = 0; i < pointCount; i += stride )
	{
		const CCVector3* p = m_cloud->getPointPersistentPtr( i );
		m_cloudZMin = std::min<double>( m_cloudZMin, p->z );
		m_cloudZMax = std::max<double>( m_cloudZMax, p->z );
	}

	const double pad = std::max( 0.001, ( m_cloudZMax - m_cloudZMin ) * 0.01 );
	m_zMinSpin->setRange( m_cloudZMin - pad, m_cloudZMax + pad );
	m_zMaxSpin->setRange( m_cloudZMin - pad, m_cloudZMax + pad );
	m_zMinSpin->setDecimals( 3 );
	m_zMaxSpin->setDecimals( 3 );
	m_zMinSpin->setValue( m_cloudZMin );
	m_zMaxSpin->setValue( m_cloudZMax );
	m_zMinSlider->setValue( 0 );
	m_zMaxSlider->setValue( SliderMax );

	m_infoLabel->setText( QString( "%1 | %2 points | Z %3 to %4" )
		.arg( m_cloud->getName() )
		.arg( pointCount )
		.arg( m_cloudZMin, 0, 'f', 3 )
		.arg( m_cloudZMax, 0, 'f', 3 ) );

	m_viewport->setCloud( m_cloud );
}

double XRayDialog::sliderToZ( int value ) const
{
	const double t = static_cast<double>( value ) / static_cast<double>( SliderMax );
	return m_cloudZMin + t * ( m_cloudZMax - m_cloudZMin );
}

int XRayDialog::zToSlider( double value ) const
{
	if ( m_cloudZMax <= m_cloudZMin )
	{
		return 0;
	}

	const double t = ( value - m_cloudZMin ) / ( m_cloudZMax - m_cloudZMin );
	return static_cast<int>( std::clamp( t, 0.0, 1.0 ) * SliderMax );
}

void XRayDialog::syncZMinFromSlider( int value )
{
	QSignalBlocker blocker( m_zMinSpin );
	m_zMinSpin->setValue( sliderToZ( value ) );
	applyControls();
}

void XRayDialog::syncZMaxFromSlider( int value )
{
	QSignalBlocker blocker( m_zMaxSpin );
	m_zMaxSpin->setValue( sliderToZ( value ) );
	applyControls();
}

void XRayDialog::applyControls()
{
	if ( !m_viewport )
	{
		return;
	}

	double zMin = m_zMinSpin->value();
	double zMax = m_zMaxSpin->value();
	if ( zMin > zMax )
	{
		std::swap( zMin, zMax );
	}

	{
		QSignalBlocker b1( m_zMinSlider );
		QSignalBlocker b2( m_zMaxSlider );
		m_zMinSlider->setValue( zToSlider( zMin ) );
		m_zMaxSlider->setValue( zToSlider( zMax ) );
	}

	m_viewport->setZRange( zMin, zMax );
	m_viewport->setGamma( m_gammaSpin->value() );
	m_viewport->setInverted( m_invertCheck->isChecked() );
}

void XRayDialog::resetZRange()
{
	m_zMinSpin->setValue( m_cloudZMin );
	m_zMaxSpin->setValue( m_cloudZMax );
	applyControls();
}
