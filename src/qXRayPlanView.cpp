#include "../include/qXRayPlanView.h"
#include "../include/NormalsOverlayLayer.h"
#include "../include/XRayOverlayLayer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <ccOverlayDialog.h>
#include <ccGuiParameters.h>
#include <ccGLWindowInterface.h>
#include <ccMainAppInterface.h>
#include <ccOctree.h>
#include <ccPointCloud.h>
#include <ccProgressDialog.h>
#include <ccScalarField.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace
{
	constexpr const char* ZSliceScalarFieldName = "__XRay_Z_Live_Slice__";

	constexpr double GridNormalsMinTriangleAngleDeg = 1.0;

	class ZSliceToolDialog : public QWidget
	{
	public:
		explicit ZSliceToolDialog( QWidget* parent, QWidget* focusTarget )
			: QWidget( parent )
			, m_focusTarget( focusTarget )
		{
			setAttribute( Qt::WA_DeleteOnClose );
			setObjectName( "ZSliceToolPanel" );
			setAutoFillBackground( true );
			setStyleSheet( "#ZSliceToolPanel { background: palette(window); border: 1px solid palette(mid); }" );
		}

		void setCloseCleanup( std::function<void()> cleanup )
		{
			m_closeCleanup = std::move( cleanup );
		}

	protected:
		void closeEvent( QCloseEvent* event ) override
		{
			releaseInteractionState();
			if ( m_closeCleanup )
			{
				m_closeCleanup();
			}
			QWidget::closeEvent( event );
			QTimer::singleShot( 0, this, [focusTarget = m_focusTarget]()
			{
				if ( focusTarget )
				{
					focusTarget->activateWindow();
					focusTarget->setFocus( Qt::OtherFocusReason );
				}
			} );
		}

	private:
		void releaseInteractionState()
		{
			if ( QWidget* mouseGrabber = QWidget::mouseGrabber() )
			{
				mouseGrabber->releaseMouse();
			}
			if ( QWidget* keyboardGrabber = QWidget::keyboardGrabber() )
			{
				keyboardGrabber->releaseKeyboard();
			}
			while ( QApplication::overrideCursor() )
			{
				QApplication::restoreOverrideCursor();
			}
			clearFocus();
			if ( parentWidget() )
			{
				parentWidget()->activateWindow();
			}
			if ( m_focusTarget )
			{
				m_focusTarget->setEnabled( true );
				m_focusTarget->activateWindow();
				m_focusTarget->setFocus( Qt::OtherFocusReason );
			}
		}

		QPointer<QWidget> m_focusTarget;
		std::function<void()> m_closeCleanup;
	};

	void addPanelTitleRow( QVBoxLayout* layout, QWidget* parent, const QString& titleText, const std::function<void()>& closeFunc )
	{
		auto* titleRow = new QWidget( parent );
		auto* titleLayout = new QHBoxLayout( titleRow );
		titleLayout->setContentsMargins( 0, 0, 0, 0 );
		titleLayout->setSpacing( 6 );

		auto* title = new QLabel( titleText, parent );
		QFont titleFont = title->font();
		titleFont.setBold( true );
		title->setFont( titleFont );
		titleLayout->addWidget( title, 1 );

		auto* closeButton = new QToolButton( parent );
		closeButton->setText( "x" );
		closeButton->setAutoRaise( true );
		closeButton->setToolTip( QObject::tr( "Close" ) );
		closeButton->setFixedSize( 22, 22 );
		titleLayout->addWidget( closeButton );
		QObject::connect( closeButton, &QToolButton::clicked, parent, closeFunc );

		layout->addWidget( titleRow );
	}

	void moveToolDialogBottomLeft( QWidget* dialog, QWidget* target )
	{
		if ( !dialog || !target )
		{
			return;
		}

		constexpr int Margin = 12;
		dialog->adjustSize();
		const int y = std::max( Margin, target->height() - dialog->height() - Margin );
		dialog->move( QPoint( Margin, y ) );
	}
}

qXRayPlanView::qXRayPlanView( QObject* parent )
	: QObject( parent )
	, ccStdPluginInterface( ":/CC/plugin/qXRayPlanView/info.json" )
{
}

void qXRayPlanView::onNewSelection( const ccHObject::Container& selectedEntities )
{
	updateActionStates( selectedEntities );
}

QList<QAction*> qXRayPlanView::getActions()
{
	if ( !m_zSliceAction )
	{
		m_zSliceAction = new QAction( tr( "Z Slice Controls" ), this );
		m_zSliceAction->setToolTip( tr( "Hide/show points by Z range. Does not change cloud colors." ) );
		m_zSliceAction->setIcon( getIcon() );
		m_zSliceAction->setEnabled( false );
		connect( m_zSliceAction, &QAction::triggered, this, &qXRayPlanView::openZSliceControls );
	}

	if ( !m_xRayOverlayAction )
	{
		m_xRayOverlayAction = new QAction( tr( "X-Ray Overlay" ), this );
		m_xRayOverlayAction->setToolTip( tr( "Open an adaptive viewport-resolution X-Ray overlay. Does not recolor the cloud." ) );
		m_xRayOverlayAction->setIcon( getIcon() );
		m_xRayOverlayAction->setEnabled( false );
		connect( m_xRayOverlayAction, &QAction::triggered, this, &qXRayPlanView::openXRayOverlay );
	}

	if ( !m_xRayControlsAction )
	{
		m_xRayControlsAction = new QAction( tr( "X-Ray Overlay Controls" ), this );
		m_xRayControlsAction->setToolTip( tr( "Adjust gamma, inversion and style for all active X-Ray overlays." ) );
		m_xRayControlsAction->setIcon( getIcon() );
		m_xRayControlsAction->setEnabled( false );
		connect( m_xRayControlsAction, &QAction::triggered, this, &qXRayPlanView::openXRayOverlayControls );
	}

	if ( !m_normalsOverlayAction )
	{
		m_normalsOverlayAction = new QAction( tr( "Normals Overlay" ), this );
		m_normalsOverlayAction->setToolTip( tr( "Open a viewport overlay colored by point normals: X red, Y green, Z blue." ) );
		m_normalsOverlayAction->setIcon( getIcon() );
		m_normalsOverlayAction->setEnabled( false );
		connect( m_normalsOverlayAction, &QAction::triggered, this, &qXRayPlanView::openNormalsOverlay );
	}

	if ( !m_normalsControlsAction )
	{
		m_normalsControlsAction = new QAction( tr( "Normals Overlay Controls" ), this );
		m_normalsControlsAction->setToolTip( tr( "Adjust background, gamma and style for all active Normals overlays." ) );
		m_normalsControlsAction->setIcon( getIcon() );
		m_normalsControlsAction->setEnabled( false );
		connect( m_normalsControlsAction, &QAction::triggered, this, &qXRayPlanView::openNormalsOverlayControls );
	}

	if ( !m_restoreAction )
	{
		m_restoreAction = new QAction( tr( "Restore Original Colors" ), this );
		m_restoreAction->setToolTip( tr( "Restore the selected cloud colors and display state saved before applying X-Ray colors." ) );
		m_restoreAction->setIcon( getIcon() );
		m_restoreAction->setEnabled( false );
		connect( m_restoreAction, &QAction::triggered, this, &qXRayPlanView::restoreOriginalColors );
	}

	return { m_xRayOverlayAction, m_xRayControlsAction, m_normalsOverlayAction, m_normalsControlsAction, m_zSliceAction, m_restoreAction };
}

std::vector<ccPointCloud*> qXRayPlanView::selectedClouds() const
{
	std::vector<ccPointCloud*> clouds;
	if ( !m_app )
	{
		return clouds;
	}

	const ccHObject::Container& selectedEntities = m_app->getSelectedEntities();
	clouds.reserve( selectedEntities.size() );
	for ( ccHObject* entity : selectedEntities )
	{
		if ( entity && entity->isA( CC_TYPES::POINT_CLOUD ) )
		{
			clouds.push_back( static_cast<ccPointCloud*>( entity ) );
		}
	}
	return clouds;
}

void qXRayPlanView::updateActionStates( const ccHObject::Container& selectedEntities )
{
	ccPointCloud* cloud = nullptr;
	size_t cloudCount = 0;
	if ( selectedEntities.size() == 1 && selectedEntities.front()->isA( CC_TYPES::POINT_CLOUD ) )
	{
		cloud = static_cast<ccPointCloud*>( selectedEntities.front() );
	}
	for ( ccHObject* entity : selectedEntities )
	{
		if ( entity && entity->isA( CC_TYPES::POINT_CLOUD ) )
		{
			++cloudCount;
		}
	}

	if ( m_zSliceAction )
	{
		m_zSliceAction->setEnabled( cloudCount > 0 );
	}

	if ( m_xRayOverlayAction )
	{
		m_xRayOverlayAction->setEnabled( cloudCount > 0 );
	}

	if ( m_xRayControlsAction )
	{
		m_xRayControlsAction->setEnabled( !m_xRayLayers.empty() );
	}

	if ( m_normalsOverlayAction )
	{
		m_normalsOverlayAction->setEnabled( cloudCount > 0 );
	}

	if ( m_normalsControlsAction )
	{
		m_normalsControlsAction->setEnabled( !m_normalsLayers.empty() );
	}

	if ( m_restoreAction )
	{
		bool canRestore = false;
		if ( !m_backups.empty() || !m_xRayLayers.empty() || !m_normalsLayers.empty() )
		{
			canRestore = true;
		}
		for ( ccHObject* entity : selectedEntities )
		{
			if ( entity && entity->isA( CC_TYPES::POINT_CLOUD ) )
			{
				const unsigned cloudId = static_cast<ccPointCloud*>( entity )->getUniqueID();
				if ( m_backups.find( cloudId ) != m_backups.end()
					|| m_xRayLayers.find( cloudId ) != m_xRayLayers.end()
					|| m_normalsLayers.find( cloudId ) != m_normalsLayers.end() )
				{
					canRestore = true;
					break;
				}
			}
		}
		m_restoreAction->setEnabled( canRestore );
	}
}

void qXRayPlanView::openZSliceControls()
{
	const std::vector<ccPointCloud*> clouds = selectedClouds();
	if ( clouds.empty() )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "Select at least one point cloud.", ccMainAppInterface::ERR_CONSOLE_MESSAGE );
		}
		return;
	}

	openZSliceDialog( clouds );
}

void qXRayPlanView::openXRayOverlay()
{
	const std::vector<ccPointCloud*> clouds = selectedClouds();
	if ( clouds.empty() )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "Select at least one point cloud.", ccMainAppInterface::ERR_CONSOLE_MESSAGE );
		}
		return;
	}

	for ( ccPointCloud* cloud : clouds )
	{
		if ( !cloud )
		{
			continue;
		}

		const unsigned cloudId = cloud->getUniqueID();
		const auto existing = m_xRayLayers.find( cloudId );
		if ( existing != m_xRayLayers.end() )
		{
			const auto cloudIt = m_xRayClouds.find( cloudId );
			if ( cloudIt != m_xRayClouds.end() && cloudIt->second )
			{
				const auto wasVisibleIt = m_xRayCloudWasVisible.find( cloudId );
				if ( wasVisibleIt != m_xRayCloudWasVisible.end() )
				{
					cloudIt->second->setVisible( wasVisibleIt->second );
				}

				const auto wasEnabledIt = m_xRayCloudWasEnabled.find( cloudId );
				if ( wasEnabledIt != m_xRayCloudWasEnabled.end() )
				{
					cloudIt->second->setEnabled( wasEnabledIt->second );
				}

				cloudIt->second->prepareDisplayForRefresh();
			}

			if ( m_app )
			{
				m_app->removeFromDB( existing->second );
			}
			m_xRayLayers.erase( existing );
			m_xRayClouds.erase( cloudId );
			m_xRayCloudWasVisible.erase( cloudId );
			m_xRayCloudWasEnabled.erase( cloudId );
			continue;
		}

		XRayOverlayLayer* layer = new XRayOverlayLayer( cloud );
		m_xRayCloudWasVisible[cloudId] = cloud->isVisible();
		m_xRayCloudWasEnabled[cloudId] = cloud->isEnabled();
		cloud->setVisible( false );
		cloud->prepareDisplayForRefresh();
		m_xRayLayers[cloudId] = layer;
		m_xRayClouds[cloudId] = cloud;
		m_app->addToDB( layer, false, true, false, true );
	}

	m_app->refreshAll();
	m_app->updateUI();
	updateActionStates( m_app->getSelectedEntities() );
}

void qXRayPlanView::openXRayOverlayControls()
{
	XRayOverlayLayer* layer = nullptr;
	if ( !m_xRayLayers.empty() )
	{
		layer = m_xRayLayers.begin()->second;
	}

	if ( !layer )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "Activate X-Ray Overlay for at least one cloud first.", ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		}
		return;
	}

	if ( !m_app || !m_app->getActiveGLWindow() )
	{
		return;
	}

	if ( m_xRayControlsDialog )
	{
		m_xRayControlsDialog->raise();
		m_xRayControlsDialog->activateWindow();
		return;
	}

	auto* dialog = new ccOverlayDialog( m_app->getMainWindow() );
	dialog->setAttribute( Qt::WA_DeleteOnClose );
	dialog->setWindowTitle( tr( "X-Ray Overlay Controls" ) );
	m_xRayControlsDialog = dialog;

	auto* layout = new QVBoxLayout( dialog );
	layout->setContentsMargins( 8, 8, 8, 8 );
	layout->setSpacing( 6 );

	addPanelTitleRow( layout, dialog, tr( "X-Ray Overlay" ), [dialog]()
	{
		dialog->stop( false );
	} );

	auto* styleCombo = new QComboBox( dialog );
	styleCombo->addItem( tr( "Monochrome" ) );
	styleCombo->addItem( tr( "Height spectral" ) );
	styleCombo->setCurrentIndex( static_cast<int>( layer->colorMode() ) );
	layout->addWidget( styleCombo );

	auto* gammaSpin = new QDoubleSpinBox( dialog );
	gammaSpin->setDecimals( 2 );
	gammaSpin->setRange( 0.10, 10.00 );
	gammaSpin->setSingleStep( 0.05 );
	gammaSpin->setValue( layer->gamma() );

	auto* gammaSlider = new QSlider( Qt::Horizontal, dialog );
	gammaSlider->setRange( 10, 1000 );
	gammaSlider->setValue( static_cast<int>( std::round( gammaSpin->value() * 100.0 ) ) );

	auto* invertCheck = new QCheckBox( tr( "Invert" ), dialog );
	invertCheck->setChecked( layer->inverted() );

	auto* gammaRow = new QWidget( dialog );
	auto* gammaLayout = new QHBoxLayout( gammaRow );
	gammaLayout->setContentsMargins( 0, 0, 0, 0 );
	gammaLayout->addWidget( new QLabel( tr( "Gamma" ), dialog ) );
	gammaLayout->addWidget( gammaSlider, 1 );
	gammaLayout->addWidget( gammaSpin );
	layout->addWidget( gammaRow );
	layout->addWidget( invertCheck );

	const auto applyControls = [=]()
	{
		const auto colorMode = static_cast<XRayOverlayLayer::ColorMode>( styleCombo->currentIndex() );
		const bool inverted = invertCheck->isChecked();
		for ( auto& item : m_xRayLayers )
		{
			XRayOverlayLayer* activeLayer = item.second;
			if ( !activeLayer )
			{
				continue;
			}
			activeLayer->setGamma( gammaSpin->value() );
			activeLayer->setColorMode( colorMode );
			activeLayer->setInverted( inverted );
			activeLayer->prepareDisplayForRefresh();
		}
		setActiveViewportBackground( inverted );
		if ( m_app && m_app->getActiveGLWindow() )
		{
			m_app->getActiveGLWindow()->redraw( false, false );
		}
	};

	connect( gammaSlider, &QSlider::valueChanged, dialog, [=]( int value )
	{
		const QSignalBlocker blocker( gammaSpin );
		gammaSpin->setValue( static_cast<double>( value ) / 100.0 );
		applyControls();
	} );
	connect( gammaSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, [=]( double value )
	{
		const QSignalBlocker blocker( gammaSlider );
		gammaSlider->setValue( static_cast<int>( std::round( value * 100.0 ) ) );
		applyControls();
	} );
	connect( styleCombo, qOverload<int>( &QComboBox::currentIndexChanged ), dialog, applyControls );
	connect( invertCheck, &QCheckBox::toggled, dialog, applyControls );
	connect( dialog, &ccOverlayDialog::processFinished, this, [this, dialog]()
	{
		if ( m_app )
		{
			m_app->unregisterOverlayDialog( dialog );
		}
		if ( m_xRayControlsDialog == dialog )
		{
			m_xRayControlsDialog = nullptr;
		}
		dialog->deleteLater();
	} );
	connect( dialog, &QDialog::destroyed, this, [this]()
	{
		m_xRayControlsDialog = nullptr;
	} );

	m_app->registerOverlayDialog( dialog, Qt::BottomRightCorner );
	dialog->linkWith( m_app->getActiveGLWindow() );
	dialog->start();
	applyControls();
}

void qXRayPlanView::openNormalsOverlay()
{
	const std::vector<ccPointCloud*> clouds = selectedClouds();
	if ( clouds.empty() )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "Select at least one point cloud.", ccMainAppInterface::ERR_CONSOLE_MESSAGE );
		}
		return;
	}

	for ( ccPointCloud* cloud : clouds )
	{
		if ( !cloud )
		{
			continue;
		}

		const unsigned cloudId = cloud->getUniqueID();
		const auto existing = m_normalsLayers.find( cloudId );
		if ( existing != m_normalsLayers.end() )
		{
			const auto cloudIt = m_normalsClouds.find( cloudId );
			if ( cloudIt != m_normalsClouds.end() && cloudIt->second )
			{
				const auto wasVisibleIt = m_normalsCloudWasVisible.find( cloudId );
				if ( wasVisibleIt != m_normalsCloudWasVisible.end() )
				{
					cloudIt->second->setVisible( wasVisibleIt->second );
				}

				const auto wasEnabledIt = m_normalsCloudWasEnabled.find( cloudId );
				if ( wasEnabledIt != m_normalsCloudWasEnabled.end() )
				{
					cloudIt->second->setEnabled( wasEnabledIt->second );
				}

				cloudIt->second->prepareDisplayForRefresh();
			}

			if ( m_app )
			{
				m_app->removeFromDB( existing->second );
			}
			m_normalsLayers.erase( existing );
			m_normalsClouds.erase( cloudId );
			m_normalsCloudWasVisible.erase( cloudId );
			m_normalsCloudWasEnabled.erase( cloudId );
			continue;
		}

		if ( !cloud->hasNormals() )
		{
			if ( m_app )
			{
				m_app->dispToConsole( QString( "Cloud '%1' has no normals. Computing normals for Normals Overlay..." ).arg( cloud->getName() ), ccMainAppInterface::STD_CONSOLE_MESSAGE );
			}

			ccProgressDialog progressDlg( true, m_app ? m_app->getMainWindow() : nullptr );
			progressDlg.setWindowTitle( tr( "Normals Overlay" ) );
			progressDlg.setLabelText( tr( "Computing normals..." ) );
			progressDlg.setAutoClose( false );

			bool computed = false;
			if ( cloud->gridCount() != 0 )
			{
				computed = cloud->computeNormalsWithGrids(
					GridNormalsMinTriangleAngleDeg,
					&progressDlg,
					ccNormalVectors::Orientation::UNDEFINED );
			}

			if ( !computed )
			{
				ccOctree::BestRadiusParams radiusParams;
				radiusParams.aimedPopulationPerCell = 12;
				radiusParams.aimedPopulationRange = 4;
				radiusParams.minCellPopulation = 5;
				const PointCoordinateType radius = ccOctree::GuessBestRadiusAutoComputeOctree(
					cloud,
					radiusParams,
					m_app ? m_app->getMainWindow() : nullptr );

				if ( radius > 0 )
				{
					if ( m_app )
					{
						m_app->dispToConsole( QString( "Normals Overlay radius: %1" ).arg( radius ), ccMainAppInterface::STD_CONSOLE_MESSAGE );
					}
					computed = cloud->computeNormalsWithOctree(
						CCCoreLib::LS,
						ccNormalVectors::Orientation::UNDEFINED,
						radius,
						&progressDlg );
				}
			}

			if ( !computed || !cloud->hasNormals() )
			{
				if ( m_app )
				{
					m_app->dispToConsole( QString( "Normals Overlay could not compute normals for '%1'." ).arg( cloud->getName() ), ccMainAppInterface::ERR_CONSOLE_MESSAGE );
				}
				continue;
			}

			cloud->prepareDisplayForRefresh();
		}

		NormalsOverlayLayer* layer = new NormalsOverlayLayer( cloud );
		m_normalsCloudWasVisible[cloudId] = cloud->isVisible();
		m_normalsCloudWasEnabled[cloudId] = cloud->isEnabled();
		cloud->setVisible( false );
		cloud->prepareDisplayForRefresh();
		m_normalsLayers[cloudId] = layer;
		m_normalsClouds[cloudId] = cloud;
		m_app->addToDB( layer, false, true, false, true );
	}

	if ( !m_normalsLayers.empty() )
	{
		NormalsOverlayLayer* layer = m_normalsLayers.begin()->second;
		if ( layer )
		{
			setActiveViewportBackgroundGray( layer->backgroundGray() );
		}
	}

	m_app->refreshAll();
	m_app->updateUI();
	updateActionStates( m_app->getSelectedEntities() );
}

void qXRayPlanView::openNormalsOverlayControls()
{
	NormalsOverlayLayer* layer = nullptr;
	if ( !m_normalsLayers.empty() )
	{
		layer = m_normalsLayers.begin()->second;
	}

	if ( !layer )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "Activate Normals Overlay for at least one cloud first.", ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		}
		return;
	}

	if ( !m_app || !m_app->getActiveGLWindow() )
	{
		return;
	}

	if ( m_normalsControlsDialog )
	{
		m_normalsControlsDialog->raise();
		m_normalsControlsDialog->activateWindow();
		return;
	}

	auto* dialog = new ccOverlayDialog( m_app->getMainWindow() );
	dialog->setAttribute( Qt::WA_DeleteOnClose );
	dialog->setWindowTitle( tr( "Normals Overlay Controls" ) );
	m_normalsControlsDialog = dialog;

	auto* layout = new QVBoxLayout( dialog );
	layout->setContentsMargins( 8, 8, 8, 8 );
	layout->setSpacing( 6 );

	addPanelTitleRow( layout, dialog, tr( "Normals Overlay" ), [dialog]()
	{
		dialog->stop( false );
	} );

	auto* modeCombo = new QComboBox( dialog );
	modeCombo->addItem( tr( "Normal RGB" ) );
	modeCombo->addItem( tr( "View filtered" ) );
	modeCombo->setCurrentIndex( static_cast<int>( layer->displayMode() ) );
	layout->addWidget( modeCombo );

	auto* paletteCombo = new QComboBox( dialog );
	paletteCombo->addItem( tr( "RGB" ) );
	paletteCombo->addItem( tr( "Soft RGB" ) );
	paletteCombo->addItem( tr( "BIM" ) );
	paletteCombo->addItem( tr( "Warm / Cool" ) );
	paletteCombo->setCurrentIndex( static_cast<int>( layer->palette() ) );
	layout->addWidget( paletteCombo );

	auto* backgroundSlider = new QSlider( Qt::Horizontal, dialog );
	backgroundSlider->setRange( 0, 255 );
	backgroundSlider->setValue( layer->backgroundGray() );

	auto* backgroundSpin = new QDoubleSpinBox( dialog );
	backgroundSpin->setDecimals( 0 );
	backgroundSpin->setRange( 0, 255 );
	backgroundSpin->setSingleStep( 1 );
	backgroundSpin->setValue( layer->backgroundGray() );

	auto* pointSizeSlider = new QSlider( Qt::Horizontal, dialog );
	pointSizeSlider->setRange( 1, 5 );
	pointSizeSlider->setValue( layer->pointRadius() );

	auto* pointSizeSpin = new QDoubleSpinBox( dialog );
	pointSizeSpin->setDecimals( 0 );
	pointSizeSpin->setRange( 1, 5 );
	pointSizeSpin->setSingleStep( 1 );
	pointSizeSpin->setValue( layer->pointRadius() );

	auto* gammaSlider = new QSlider( Qt::Horizontal, dialog );
	gammaSlider->setRange( 10, 1000 );
	gammaSlider->setValue( static_cast<int>( std::round( layer->gamma() * 100.0 ) ) );

	auto* gammaSpin = new QDoubleSpinBox( dialog );
	gammaSpin->setDecimals( 2 );
	gammaSpin->setRange( 0.10, 10.00 );
	gammaSpin->setSingleStep( 0.05 );
	gammaSpin->setValue( layer->gamma() );

	auto* filterSlider = new QSlider( Qt::Horizontal, dialog );
	filterSlider->setRange( 0, 100 );
	filterSlider->setValue( static_cast<int>( std::round( layer->filterStrength() * 100.0 ) ) );

	auto* filterSpin = new QDoubleSpinBox( dialog );
	filterSpin->setDecimals( 2 );
	filterSpin->setRange( 0.00, 1.00 );
	filterSpin->setSingleStep( 0.05 );
	filterSpin->setValue( layer->filterStrength() );

	auto* backgroundRow = new QWidget( dialog );
	auto* backgroundLayout = new QHBoxLayout( backgroundRow );
	backgroundLayout->setContentsMargins( 0, 0, 0, 0 );
	backgroundLayout->addWidget( new QLabel( tr( "Background" ), dialog ) );
	backgroundLayout->addWidget( backgroundSlider, 1 );
	backgroundLayout->addWidget( backgroundSpin );
	layout->addWidget( backgroundRow );

	auto* pointSizeRow = new QWidget( dialog );
	auto* pointSizeLayout = new QHBoxLayout( pointSizeRow );
	pointSizeLayout->setContentsMargins( 0, 0, 0, 0 );
	pointSizeLayout->addWidget( new QLabel( tr( "Point size" ), dialog ) );
	pointSizeLayout->addWidget( pointSizeSlider, 1 );
	pointSizeLayout->addWidget( pointSizeSpin );
	layout->addWidget( pointSizeRow );

	auto* gammaRow = new QWidget( dialog );
	auto* gammaLayout = new QHBoxLayout( gammaRow );
	gammaLayout->setContentsMargins( 0, 0, 0, 0 );
	gammaLayout->addWidget( new QLabel( tr( "Gamma" ), dialog ) );
	gammaLayout->addWidget( gammaSlider, 1 );
	gammaLayout->addWidget( gammaSpin );
	layout->addWidget( gammaRow );

	auto* filterRow = new QWidget( dialog );
	auto* filterLayout = new QHBoxLayout( filterRow );
	filterLayout->setContentsMargins( 0, 0, 0, 0 );
	filterLayout->addWidget( new QLabel( tr( "Filter strength" ), dialog ) );
	filterLayout->addWidget( filterSlider, 1 );
	filterLayout->addWidget( filterSpin );
	layout->addWidget( filterRow );

	const auto applyControls = [=]()
	{
		const int backgroundGray = static_cast<int>( std::round( backgroundSpin->value() ) );
		const int pointRadius = static_cast<int>( std::round( pointSizeSpin->value() ) );
		const auto displayMode = static_cast<NormalsOverlayLayer::DisplayMode>( modeCombo->currentIndex() );
		const auto palette = static_cast<NormalsOverlayLayer::Palette>( paletteCombo->currentIndex() );
		for ( auto& item : m_normalsLayers )
		{
			NormalsOverlayLayer* activeLayer = item.second;
			if ( !activeLayer )
			{
				continue;
			}
			activeLayer->setBackgroundGray( backgroundGray );
			activeLayer->setPointRadius( pointRadius );
			activeLayer->setDisplayMode( displayMode );
			activeLayer->setPalette( palette );
			activeLayer->setGamma( gammaSpin->value() );
			activeLayer->setFilterStrength( filterSpin->value() );
			activeLayer->prepareDisplayForRefresh();
		}
		setActiveViewportBackgroundGray( backgroundGray );
		if ( m_app && m_app->getActiveGLWindow() )
		{
			m_app->getActiveGLWindow()->redraw( false, false );
		}
	};

	connect( backgroundSlider, &QSlider::valueChanged, dialog, [=]( int value )
	{
		const QSignalBlocker blocker( backgroundSpin );
		backgroundSpin->setValue( value );
		applyControls();
	} );
	connect( backgroundSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, [=]( double value )
	{
		const QSignalBlocker blocker( backgroundSlider );
		backgroundSlider->setValue( static_cast<int>( std::round( value ) ) );
		applyControls();
	} );
	connect( pointSizeSlider, &QSlider::valueChanged, dialog, [=]( int value )
	{
		const QSignalBlocker blocker( pointSizeSpin );
		pointSizeSpin->setValue( value );
		applyControls();
	} );
	connect( pointSizeSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, [=]( double value )
	{
		const QSignalBlocker blocker( pointSizeSlider );
		pointSizeSlider->setValue( static_cast<int>( std::round( value ) ) );
		applyControls();
	} );
	connect( gammaSlider, &QSlider::valueChanged, dialog, [=]( int value )
	{
		const QSignalBlocker blocker( gammaSpin );
		gammaSpin->setValue( static_cast<double>( value ) / 100.0 );
		applyControls();
	} );
	connect( gammaSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, [=]( double value )
	{
		const QSignalBlocker blocker( gammaSlider );
		gammaSlider->setValue( static_cast<int>( std::round( value * 100.0 ) ) );
		applyControls();
	} );
	connect( filterSlider, &QSlider::valueChanged, dialog, [=]( int value )
	{
		const QSignalBlocker blocker( filterSpin );
		filterSpin->setValue( static_cast<double>( value ) / 100.0 );
		applyControls();
	} );
	connect( filterSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, [=]( double value )
	{
		const QSignalBlocker blocker( filterSlider );
		filterSlider->setValue( static_cast<int>( std::round( value * 100.0 ) ) );
		applyControls();
	} );
	connect( modeCombo, qOverload<int>( &QComboBox::currentIndexChanged ), dialog, applyControls );
	connect( paletteCombo, qOverload<int>( &QComboBox::currentIndexChanged ), dialog, applyControls );
	connect( dialog, &ccOverlayDialog::processFinished, this, [this, dialog]()
	{
		if ( m_app )
		{
			m_app->unregisterOverlayDialog( dialog );
		}
		if ( m_normalsControlsDialog == dialog )
		{
			m_normalsControlsDialog = nullptr;
		}
		dialog->deleteLater();
	} );
	connect( dialog, &QDialog::destroyed, this, [this]()
	{
		m_normalsControlsDialog = nullptr;
	} );

	m_app->registerOverlayDialog( dialog, Qt::TopRightCorner );
	dialog->linkWith( m_app->getActiveGLWindow() );
	dialog->start();
	applyControls();
}

void qXRayPlanView::restoreOriginalColors()
{
	std::vector<ccPointCloud*> clouds = selectedClouds();
	const auto addUniqueCloud = [&clouds]( ccPointCloud* cloud )
	{
		if ( !cloud )
		{
			return;
		}
		const unsigned cloudId = cloud->getUniqueID();
		for ( ccPointCloud* existingCloud : clouds )
		{
			if ( existingCloud && existingCloud->getUniqueID() == cloudId )
			{
				return;
			}
		}
		clouds.push_back( cloud );
	};

	for ( const auto& item : m_xRayClouds )
	{
		addUniqueCloud( item.second );
	}
	for ( const auto& item : m_normalsClouds )
	{
		addUniqueCloud( item.second );
	}
	if ( clouds.empty() && m_app )
	{
		const ccHObject::Container& selectedEntities = m_app->getSelectedEntities();
		for ( ccHObject* entity : selectedEntities )
		{
			if ( entity && entity->isA( CC_TYPES::POINT_CLOUD ) )
			{
				addUniqueCloud( static_cast<ccPointCloud*>( entity ) );
			}
		}
	}

	if ( clouds.empty() )
	{
		if ( m_app )
		{
			m_app->dispToConsole( "No active overlay or selected point cloud to restore.", ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		}
		return;
	}

	if ( m_xRayControlsDialog )
	{
		m_xRayControlsDialog->stop( false );
	}
	if ( m_normalsControlsDialog )
	{
		m_normalsControlsDialog->stop( false );
	}

	bool restoredSomething = false;
	for ( ccPointCloud* cloud : clouds )
	{
		if ( !cloud )
		{
			continue;
		}

		const unsigned cloudId = cloud->getUniqueID();
		const auto xRayIt = m_xRayLayers.find( cloudId );
		if ( xRayIt != m_xRayLayers.end() )
		{
			const auto wasVisibleIt = m_xRayCloudWasVisible.find( cloudId );
			if ( wasVisibleIt != m_xRayCloudWasVisible.end() )
			{
				cloud->setVisible( wasVisibleIt->second );
			}
			const auto wasEnabledIt = m_xRayCloudWasEnabled.find( cloudId );
			if ( wasEnabledIt != m_xRayCloudWasEnabled.end() )
			{
				cloud->setEnabled( wasEnabledIt->second );
			}
			if ( m_app )
			{
				m_app->removeFromDB( xRayIt->second );
			}
			m_xRayLayers.erase( xRayIt );
			m_xRayClouds.erase( cloudId );
			m_xRayCloudWasVisible.erase( cloudId );
			m_xRayCloudWasEnabled.erase( cloudId );
			restoredSomething = true;
		}

		const auto normalsIt = m_normalsLayers.find( cloudId );
		if ( normalsIt != m_normalsLayers.end() )
		{
			const auto wasVisibleIt = m_normalsCloudWasVisible.find( cloudId );
			if ( wasVisibleIt != m_normalsCloudWasVisible.end() )
			{
				cloud->setVisible( wasVisibleIt->second );
			}
			const auto wasEnabledIt = m_normalsCloudWasEnabled.find( cloudId );
			if ( wasEnabledIt != m_normalsCloudWasEnabled.end() )
			{
				cloud->setEnabled( wasEnabledIt->second );
			}
			if ( m_app )
			{
				m_app->removeFromDB( normalsIt->second );
			}
			m_normalsLayers.erase( normalsIt );
			m_normalsClouds.erase( cloudId );
			m_normalsCloudWasVisible.erase( cloudId );
			m_normalsCloudWasEnabled.erase( cloudId );
			restoredSomething = true;
		}

		if ( restoreBackup( cloud ) )
		{
			restoredSomething = true;
		}
		else
		{
			cloud->unallocateVisibilityArray();
		}

		cloud->prepareDisplayForRefresh();
	}

	setActiveViewportBackground( false );
	m_app->refreshAll();
	m_app->updateUI();
	m_app->dispToConsole(
		restoredSomething ? "Original display restored." : "No saved color backup found. Cleared Z visibility slice and restored white background.",
		restoredSomething ? ccMainAppInterface::STD_CONSOLE_MESSAGE : ccMainAppInterface::WRN_CONSOLE_MESSAGE );
	updateActionStates( m_app->getSelectedEntities() );
}

void qXRayPlanView::openZSliceDialog( const std::vector<ccPointCloud*>& clouds )
{
	if ( clouds.empty() )
	{
		return;
	}

	if ( m_controlsDialog )
	{
		if ( m_app && m_app->getActiveGLWindow() )
		{
			moveToolDialogBottomLeft( m_controlsDialog, m_app->getActiveGLWindow()->asWidget() );
		}
		m_controlsDialog->raise();
		m_controlsDialog->activateWindow();
		return;
	}

	if ( !m_app || !m_app->getActiveGLWindow() )
	{
		return;
	}

	bool hasPoint = false;
	double zMin = 0.0;
	double zMax = 0.0;
	for ( ccPointCloud* cloud : clouds )
	{
		if ( !cloud || cloud->size() == 0 )
		{
			continue;
		}
		for ( unsigned i = 0; i < cloud->size(); ++i )
		{
			const CCVector3* p = cloud->getPointPersistentPtr( i );
			if ( !hasPoint )
			{
				zMin = p->z;
				zMax = p->z;
				hasPoint = true;
			}
			else
			{
				zMin = std::min<double>( zMin, p->z );
				zMax = std::max<double>( zMax, p->z );
			}
		}
	}

	if ( !hasPoint )
	{
		return;
	}

	const double zSpan = std::max( 1e-9, zMax - zMin );
	const double initialCenter = 0.5 * ( zMin + zMax );
	constexpr int SliderMax = 10000;

	const auto sliderToZ = [zMin, zSpan]( int value ) -> double
	{
		return zMin + zSpan * static_cast<double>( value ) / static_cast<double>( SliderMax );
	};

	const auto zToSlider = [zMin, zSpan]( double z ) -> int
	{
		return static_cast<int>( std::round( static_cast<double>( SliderMax ) * ( z - zMin ) / zSpan ) );
	};

	QWidget* focusTarget = m_app->getActiveGLWindow() ? m_app->getActiveGLWindow()->asWidget() : m_app->getMainWindow();
	auto* dialog = new ZSliceToolDialog( focusTarget, focusTarget );
	dialog->setWindowTitle( tr( "Z Slice" ) );
	m_controlsDialog = dialog;

	auto* mainLayout = new QVBoxLayout( dialog );
	mainLayout->setContentsMargins( 8, 8, 8, 8 );
	mainLayout->setSpacing( 6 );

	addPanelTitleRow( mainLayout, dialog, tr( "Z Slice" ), [dialog]()
	{
		dialog->close();
	} );

	auto* modeCombo = new QComboBox( dialog );
	modeCombo->addItem( tr( "Center / Thickness" ) );
	modeCombo->addItem( tr( "Z min / Z max" ) );
	mainLayout->addWidget( modeCombo );

	auto* centerSlider = new QSlider( Qt::Horizontal, dialog );
	centerSlider->setRange( 0, SliderMax );
	centerSlider->setValue( std::clamp( zToSlider( initialCenter ), 0, SliderMax ) );

	auto* thicknessSlider = new QSlider( Qt::Horizontal, dialog );
	thicknessSlider->setRange( 1, SliderMax );
	thicknessSlider->setValue( SliderMax );

	auto* centerSpin = new QDoubleSpinBox( dialog );
	centerSpin->setDecimals( 3 );
	centerSpin->setRange( zMin, zMax );
	centerSpin->setSingleStep( std::max( 0.001, zSpan / 1000.0 ) );
	centerSpin->setValue( initialCenter );

	auto* thicknessSpin = new QDoubleSpinBox( dialog );
	thicknessSpin->setDecimals( 3 );
	thicknessSpin->setRange( std::max( 0.001, zSpan / static_cast<double>( SliderMax ) ), zSpan );
	thicknessSpin->setSingleStep( std::max( 0.001, zSpan / 1000.0 ) );
	thicknessSpin->setValue( zSpan );

	auto* zMinSlider = new QSlider( Qt::Horizontal, dialog );
	zMinSlider->setRange( 0, SliderMax );
	zMinSlider->setValue( 0 );

	auto* zMaxSlider = new QSlider( Qt::Horizontal, dialog );
	zMaxSlider->setRange( 0, SliderMax );
	zMaxSlider->setValue( SliderMax );

	auto* zMinSpin = new QDoubleSpinBox( dialog );
	zMinSpin->setDecimals( 3 );
	zMinSpin->setRange( zMin, zMax );
	zMinSpin->setSingleStep( std::max( 0.001, zSpan / 1000.0 ) );
	zMinSpin->setValue( zMin );

	auto* zMaxSpin = new QDoubleSpinBox( dialog );
	zMaxSpin->setDecimals( 3 );
	zMaxSpin->setRange( zMin, zMax );
	zMaxSpin->setSingleStep( std::max( 0.001, zSpan / 1000.0 ) );
	zMaxSpin->setValue( zMax );

	auto* rangeLabel = new QLabel( dialog );

	auto* centerRow = new QWidget( dialog );
	auto* centerLayout = new QHBoxLayout( centerRow );
	centerLayout->setContentsMargins( 0, 0, 0, 0 );
	centerLayout->addWidget( new QLabel( tr( "Center" ), dialog ) );
	centerLayout->addWidget( centerSlider, 1 );
	centerLayout->addWidget( centerSpin );

	auto* thicknessRow = new QWidget( dialog );
	auto* thicknessLayout = new QHBoxLayout( thicknessRow );
	thicknessLayout->setContentsMargins( 0, 0, 0, 0 );
	thicknessLayout->addWidget( new QLabel( tr( "Thickness" ), dialog ) );
	thicknessLayout->addWidget( thicknessSlider, 1 );
	thicknessLayout->addWidget( thicknessSpin );

	auto* zMinRow = new QWidget( dialog );
	auto* zMinLayout = new QHBoxLayout( zMinRow );
	zMinLayout->setContentsMargins( 0, 0, 0, 0 );
	zMinLayout->addWidget( new QLabel( tr( "Z min" ), dialog ) );
	zMinLayout->addWidget( zMinSlider, 1 );
	zMinLayout->addWidget( zMinSpin );

	auto* zMaxRow = new QWidget( dialog );
	auto* zMaxLayout = new QHBoxLayout( zMaxRow );
	zMaxLayout->setContentsMargins( 0, 0, 0, 0 );
	zMaxLayout->addWidget( new QLabel( tr( "Z max" ), dialog ) );
	zMaxLayout->addWidget( zMaxSlider, 1 );
	zMaxLayout->addWidget( zMaxSpin );

	mainLayout->addWidget( centerRow );
	mainLayout->addWidget( thicknessRow );
	mainLayout->addWidget( zMinRow );
	mainLayout->addWidget( zMaxRow );
	mainLayout->addWidget( rangeLabel );

	zMinRow->hide();
	zMaxRow->hide();

	const auto applyNow = [=]()
	{
		double currentZMin = zMin;
		double currentZMax = zMax;
		if ( modeCombo->currentIndex() == 0 )
		{
			const double halfThickness = 0.5 * thicknessSpin->value();
			currentZMin = std::max( zMin, centerSpin->value() - halfThickness );
			currentZMax = std::min( zMax, centerSpin->value() + halfThickness );
		}
		else
		{
			currentZMin = std::min( zMinSpin->value(), zMaxSpin->value() );
			currentZMax = std::max( zMinSpin->value(), zMaxSpin->value() );
		}
		rangeLabel->setText( QString( "%1  to  %2" ).arg( currentZMin, 0, 'f', 3 ).arg( currentZMax, 0, 'f', 3 ) );

		bool updated = false;
		for ( ccPointCloud* cloud : clouds )
		{
			if ( !cloud )
			{
				continue;
			}

			bool overlayUpdated = false;
			const unsigned cloudId = cloud->getUniqueID();
			const auto xRayIt = m_xRayLayers.find( cloudId );
			if ( xRayIt != m_xRayLayers.end() )
			{
				xRayIt->second->setZRange( currentZMin, currentZMax );
				xRayIt->second->prepareDisplayForRefresh();
				overlayUpdated = true;
			}
			const auto normalsIt = m_normalsLayers.find( cloudId );
			if ( normalsIt != m_normalsLayers.end() )
			{
				normalsIt->second->setZRange( currentZMin, currentZMax );
				normalsIt->second->prepareDisplayForRefresh();
				overlayUpdated = true;
			}

			if ( overlayUpdated )
			{
				updated = true;
			}
			else if ( applyZVisibility( cloud, currentZMin, currentZMax, false ) )
			{
				updated = true;
			}
		}

		if ( updated )
		{
			if ( m_app && m_app->getActiveGLWindow() )
			{
				m_app->getActiveGLWindow()->redraw( false, false );
			}
			if ( m_app )
			{
				m_app->updateUI();
			}
		}
		else if ( m_app )
		{
			m_app->dispToConsole( "Failed to update Z live slice.", ccMainAppInterface::WRN_CONSOLE_MESSAGE );
		}

		updateActionStates( m_app->getSelectedEntities() );
	};

	auto* liveApplyTimer = new QTimer( dialog );
	liveApplyTimer->setSingleShot( true );
	liveApplyTimer->setInterval( 80 );
	connect( liveApplyTimer, &QTimer::timeout, dialog, applyNow );

	const auto scheduleApply = [=]()
	{
		liveApplyTimer->start();
	};

	const auto syncCenterFromSlider = [=]()
	{
		const QSignalBlocker blocker( centerSpin );
		centerSpin->setValue( sliderToZ( centerSlider->value() ) );
		scheduleApply();
	};

	const auto syncThicknessFromSlider = [=]()
	{
		const QSignalBlocker blocker( thicknessSpin );
		thicknessSpin->setValue( zSpan * static_cast<double>( thicknessSlider->value() ) / static_cast<double>( SliderMax ) );
		scheduleApply();
	};

	const auto syncCenterFromSpin = [=]()
	{
		const QSignalBlocker blocker( centerSlider );
		centerSlider->setValue( std::clamp( zToSlider( centerSpin->value() ), 0, SliderMax ) );
		scheduleApply();
	};

	const auto syncThicknessFromSpin = [=]()
	{
		const QSignalBlocker blocker( thicknessSlider );
		thicknessSlider->setValue( std::clamp( static_cast<int>( std::round( static_cast<double>( SliderMax ) * thicknessSpin->value() / zSpan ) ), 1, SliderMax ) );
		scheduleApply();
	};

	const auto syncZMinFromSlider = [=]()
	{
		const QSignalBlocker blocker( zMinSpin );
		zMinSpin->setValue( sliderToZ( zMinSlider->value() ) );
		scheduleApply();
	};

	const auto syncZMaxFromSlider = [=]()
	{
		const QSignalBlocker blocker( zMaxSpin );
		zMaxSpin->setValue( sliderToZ( zMaxSlider->value() ) );
		scheduleApply();
	};

	const auto syncZMinFromSpin = [=]()
	{
		const QSignalBlocker blocker( zMinSlider );
		zMinSlider->setValue( std::clamp( zToSlider( zMinSpin->value() ), 0, SliderMax ) );
		scheduleApply();
	};

	const auto syncZMaxFromSpin = [=]()
	{
		const QSignalBlocker blocker( zMaxSlider );
		zMaxSlider->setValue( std::clamp( zToSlider( zMaxSpin->value() ), 0, SliderMax ) );
		scheduleApply();
	};

	connect( centerSlider, &QSlider::valueChanged, dialog, syncCenterFromSlider );
	connect( thicknessSlider, &QSlider::valueChanged, dialog, syncThicknessFromSlider );
	connect( centerSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, syncCenterFromSpin );
	connect( thicknessSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, syncThicknessFromSpin );
	connect( zMinSlider, &QSlider::valueChanged, dialog, syncZMinFromSlider );
	connect( zMaxSlider, &QSlider::valueChanged, dialog, syncZMaxFromSlider );
	connect( zMinSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, syncZMinFromSpin );
	connect( zMaxSpin, qOverload<double>( &QDoubleSpinBox::valueChanged ), dialog, syncZMaxFromSpin );
	connect( modeCombo, qOverload<int>( &QComboBox::currentIndexChanged ), dialog, [=]( int index )
	{
		const bool centerMode = index == 0;
		centerRow->setVisible( centerMode );
		thicknessRow->setVisible( centerMode );
		zMinRow->setVisible( !centerMode );
		zMaxRow->setVisible( !centerMode );
		scheduleApply();
	} );
	dialog->setCloseCleanup( [this, liveApplyTimer]()
	{
		if ( liveApplyTimer )
		{
			liveApplyTimer->stop();
		}
		m_controlsDialog = nullptr;
		if ( m_app )
		{
			if ( m_app->getActiveGLWindow() )
			{
				m_app->getActiveGLWindow()->redraw( false, false );
			}
			m_app->updateUI();
		}
	} );
	connect( dialog, &QDialog::destroyed, this, [this]()
	{
		m_controlsDialog = nullptr;
	} );

	dialog->show();
	moveToolDialogBottomLeft( dialog, focusTarget );
	dialog->raise();
	applyNow();
}

bool qXRayPlanView::ensureZScalarField( ccPointCloud* cloud, ColorBackup& backup )
{
	if ( !cloud || cloud->size() == 0 )
	{
		return false;
	}

	int zSfIndex = cloud->getScalarFieldIndexByName( ZSliceScalarFieldName );
	bool fillZValues = false;
	if ( zSfIndex < 0 )
	{
		zSfIndex = cloud->addScalarField( ZSliceScalarFieldName );
		if ( zSfIndex < 0 )
		{
			return false;
		}
		backup.createdZScalarField = true;
		fillZValues = true;
	}

	CCCoreLib::ScalarField* zSf = cloud->getScalarField( zSfIndex );
	if ( !zSf )
	{
		return false;
	}

	if ( zSf->currentSize() != cloud->size() )
	{
		if ( !zSf->resizeSafe( cloud->size() ) )
		{
			return false;
		}
		fillZValues = true;
	}

	if ( fillZValues )
	{
		for ( unsigned i = 0; i < cloud->size(); ++i )
		{
			const CCVector3* p = cloud->getPointPersistentPtr( i );
			zSf->setValue( i, static_cast<ScalarType>( p->z ) );
		}
		zSf->computeMinAndMax();
	}

	backup.zScalarFieldIndex = zSfIndex;
	return true;
}

bool qXRayPlanView::applyZVisibility( ccPointCloud* cloud, double zMin, double zMax, bool inverted )
{
	if ( !cloud || cloud->size() == 0 )
	{
		return false;
	}

	const unsigned cloudId = cloud->getUniqueID();
	auto backupIt = m_backups.find( cloudId );
	if ( backupIt == m_backups.end() )
	{
		ColorBackup backup;
		backup.hadColors = cloud->hasColors();
		backup.colorsShown = cloud->colorsShown();
		backup.sfShown = cloud->sfShown();
		backup.displayedScalarFieldIndex = cloud->getCurrentDisplayedScalarFieldIndex();
		backup.currentInScalarFieldIndex = cloud->getCurrentInScalarFieldIndex();
		backup.currentOutScalarFieldIndex = cloud->getCurrentOutScalarFieldIndex();
		backup.hadVisibility = cloud->isVisibilityTableInstantiated();
		if ( backup.hadVisibility )
		{
			backup.visibility = cloud->getTheVisibilityArray();
		}
		if ( m_app && m_app->getActiveGLWindow() )
		{
			const ccGui::ParamStruct& displayParams = m_app->getActiveGLWindow()->getDisplayParameters();
			backup.hadBackground = true;
			backup.backgroundCol = displayParams.backgroundCol;
			backup.drawBackgroundGradient = displayParams.drawBackgroundGradient;
		}

		auto insertResult = m_backups.emplace( cloudId, std::move( backup ) );
		backupIt = insertResult.first;
	}

	ColorBackup& backup = backupIt->second;
	if ( !cloud->resetVisibilityArray() )
	{
		return false;
	}

	const double currentZMin = std::min( zMin, zMax );
	const double currentZMax = std::max( zMin, zMax );
	ccGenericPointCloud::VisibilityTableType& visibility = cloud->getTheVisibilityArray();
	const unsigned pointCount = cloud->size();
	for ( unsigned i = 0; i < pointCount; ++i )
	{
		const CCVector3* p = cloud->getPointPersistentPtr( i );
		const bool wasVisible = !backup.hadVisibility
			|| ( i < backup.visibility.size() && backup.visibility[i] == CCCoreLib::POINT_VISIBLE );
		if ( !wasVisible || p->z < currentZMin || p->z > currentZMax )
		{
			visibility[i] = CCCoreLib::POINT_HIDDEN;
		}
	}

	cloud->releaseVBOs();
	cloud->prepareDisplayForRefresh();
	setActiveViewportBackground( inverted );
	return true;
}

bool qXRayPlanView::restoreBackup( ccPointCloud* cloud )
{
	if ( !cloud )
	{
		return false;
	}

	const auto it = m_backups.find( cloud->getUniqueID() );
	if ( it == m_backups.end() )
	{
		return false;
	}

	const ColorBackup& backup = it->second;

	if ( backup.colorsBackedUp )
	{
		if ( backup.colors.size() != cloud->size() || !cloud->resizeTheRGBTable( false ) )
		{
			return false;
		}

		for ( unsigned i = 0; i < cloud->size(); ++i )
		{
			cloud->setPointColor( i, backup.colors[i] );
		}
		cloud->colorsHaveChanged();
	}
	else if ( !backup.hadColors )
	{
		cloud->unallocateColors();
	}

	cloud->showColors( backup.colorsShown );
	cloud->showSF( backup.sfShown );
	if ( backup.hadVisibility && backup.visibility.size() == cloud->size() )
	{
		if ( !cloud->resetVisibilityArray() )
		{
			return false;
		}
		cloud->getTheVisibilityArray() = backup.visibility;
	}
	else
	{
		cloud->unallocateVisibilityArray();
	}
	cloud->setCurrentDisplayedScalarField( backup.displayedScalarFieldIndex );
	cloud->setCurrentInScalarField( backup.currentInScalarFieldIndex );
	cloud->setCurrentOutScalarField( backup.currentOutScalarFieldIndex );
	if ( backup.createdZScalarField && backup.zScalarFieldIndex >= 0 )
	{
		cloud->deleteScalarField( backup.zScalarFieldIndex );
	}
	cloud->prepareDisplayForRefresh();

	if ( backup.hadBackground && m_app && m_app->getActiveGLWindow() )
	{
		ccGui::ParamStruct displayParams = m_app->getActiveGLWindow()->getDisplayParameters();
		displayParams.backgroundCol = backup.backgroundCol;
		displayParams.drawBackgroundGradient = backup.drawBackgroundGradient;
		m_app->getActiveGLWindow()->setDisplayParameters( displayParams, true );
	}

	m_backups.erase( it );
	return true;
}

void qXRayPlanView::setActiveViewportBackground( bool inverted )
{
	setActiveViewportBackgroundGray( inverted ? 0 : 255 );
}

void qXRayPlanView::setActiveViewportBackgroundGray( int gray )
{
	if ( !m_app || !m_app->getActiveGLWindow() )
	{
		return;
	}

	const int clampedGray = std::clamp( gray, 0, 255 );
	ccGui::ParamStruct displayParams = m_app->getActiveGLWindow()->getDisplayParameters();
	displayParams.backgroundCol = ccColor::Rgbub( clampedGray, clampedGray, clampedGray );
	displayParams.drawBackgroundGradient = false;
	m_app->getActiveGLWindow()->setDisplayParameters( displayParams, true );
}
