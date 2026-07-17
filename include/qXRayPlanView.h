#pragma once

#include "ccStdPluginInterface.h"

#include <ccColorTypes.h>

#include <QPointer>

#include <unordered_map>
#include <vector>

class QAction;
class QWidget;
class ccOverlayDialog;
class ccPointCloud;
class NormalsOverlayLayer;
class XRayOverlayLayer;

class qXRayPlanView : public QObject, public ccStdPluginInterface
{
	Q_OBJECT
	Q_INTERFACES( ccPluginInterface ccStdPluginInterface )
	Q_PLUGIN_METADATA( IID "cccorp.cloudcompare.plugin.qXRayPlanView" FILE "../info.json" )

public:
	explicit qXRayPlanView( QObject* parent = nullptr );
	~qXRayPlanView() override = default;

	void onNewSelection( const ccHObject::Container& selectedEntities ) override;
	QList<QAction*> getActions() override;

private slots:
	void openZSliceControls();
	void openXRayOverlay();
	void openXRayOverlayControls();
	void openNormalsOverlay();
	void openNormalsOverlayControls();
	void restoreOriginalColors();

private:
	struct ColorBackup
	{
		bool hadColors = false;
		bool colorsBackedUp = false;
		bool colorsShown = false;
		bool sfShown = false;
		bool hadBackground = false;
		ccColor::Rgbub backgroundCol = ccColor::Rgbub( 0, 0, 0 );
		bool drawBackgroundGradient = false;
		int displayedScalarFieldIndex = -1;
		int currentInScalarFieldIndex = -1;
		int currentOutScalarFieldIndex = -1;
		int zScalarFieldIndex = -1;
		bool createdZScalarField = false;
		std::vector<ccColor::Rgba> colors;
	};

	std::vector<ccPointCloud*> selectedClouds() const;
	void updateActionStates( const ccHObject::Container& selectedEntities );
	void openZSliceDialog( const std::vector<ccPointCloud*>& clouds );
	bool ensureZScalarField( ccPointCloud* cloud, ColorBackup& backup );
	bool applyZVisibility( ccPointCloud* cloud, double zMin, double zMax, bool inverted );
	bool restoreBackup( ccPointCloud* cloud );
	void setActiveViewportBackground( bool inverted );
	void setActiveViewportBackgroundGray( int gray );

	QAction* m_zSliceAction = nullptr;
	QAction* m_xRayOverlayAction = nullptr;
	QAction* m_xRayControlsAction = nullptr;
	QAction* m_normalsOverlayAction = nullptr;
	QAction* m_normalsControlsAction = nullptr;
	QAction* m_restoreAction = nullptr;
	QPointer<QWidget> m_controlsDialog;
	QPointer<ccOverlayDialog> m_xRayControlsDialog;
	QPointer<ccOverlayDialog> m_normalsControlsDialog;
	std::unordered_map<unsigned, XRayOverlayLayer*> m_xRayLayers;
	std::unordered_map<unsigned, ccPointCloud*> m_xRayClouds;
	std::unordered_map<unsigned, bool> m_xRayCloudWasVisible;
	std::unordered_map<unsigned, bool> m_xRayCloudWasEnabled;
	std::unordered_map<unsigned, NormalsOverlayLayer*> m_normalsLayers;
	std::unordered_map<unsigned, ccPointCloud*> m_normalsClouds;
	std::unordered_map<unsigned, bool> m_normalsCloudWasVisible;
	std::unordered_map<unsigned, bool> m_normalsCloudWasEnabled;
	std::unordered_map<unsigned, ColorBackup> m_backups;
};
