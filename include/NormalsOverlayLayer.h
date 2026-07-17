#pragma once

#include <ccHObject.h>
#include <ccGenericGLDisplay.h>

#include <QImage>

#include <memory>
#include <vector>

class ccPointCloud;
class QOpenGLTexture;

class NormalsOverlayLayer : public ccHObject
{
public:
	enum class DisplayMode
	{
		NormalRGB = 0,
		ViewFiltered = 1,
	};

	enum class Palette
	{
		RGB = 0,
		SoftRGB = 1,
		BIM = 2,
		WarmCool = 3,
	};

	explicit NormalsOverlayLayer( ccPointCloud* cloud );
	~NormalsOverlayLayer() override;

	CC_CLASS_ENUM getClassID() const override
	{
		return CC_TYPES::HIERARCHY_OBJECT;
	}

	bool isSerializable() const override
	{
		return false;
	}

	void setBackgroundGray( int value );
	void setPointRadius( int value );
	void setDisplayMode( DisplayMode mode );
	void setPalette( Palette palette );
	void setGamma( double value );
	void setFilterStrength( double value );
	void setZRange( double zMin, double zMax );
	int backgroundGray() const { return m_backgroundGray; }
	int pointRadius() const { return m_pointRadius; }
	DisplayMode displayMode() const { return m_displayMode; }
	Palette palette() const { return m_palette; }
	double gamma() const { return m_gamma; }
	double filterStrength() const { return m_filterStrength; }
	double zMin() const { return m_zMin; }
	double zMax() const { return m_zMax; }

protected:
	void drawMeOnly( CC_DRAW_CONTEXT& context ) override;
	ccBBox getOwnFitBB( ccGLMatrix& trans ) override;

private:
	struct CachedPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float nx = 0.0f;
		float ny = 0.0f;
		float nz = 0.0f;
	};

	void updateCache();
	void invalidateImage();
	void renderImage( CC_DRAW_CONTEXT& context, const ccGLCameraParameters& camera );
	bool ensureTexture();
	size_t visibilitySignature() const;

	ccPointCloud* m_cloud = nullptr;
	std::vector<CachedPoint> m_points;
	size_t m_visibilitySignature = 0;
	QImage m_image;
	std::unique_ptr<QOpenGLTexture> m_texture;
	bool m_textureDirty = true;
	ccGLCameraParameters m_lastCamera;
	bool m_hasLastCamera = false;
	int m_lastW = 0;
	int m_lastH = 0;
	int m_backgroundGray = 0;
	int m_pointRadius = 1;
	DisplayMode m_displayMode = DisplayMode::NormalRGB;
	Palette m_palette = Palette::SoftRGB;
	double m_gamma = 0.65;
	double m_filterStrength = 0.65;
	double m_zMin = 0.0;
	double m_zMax = 0.0;
};
