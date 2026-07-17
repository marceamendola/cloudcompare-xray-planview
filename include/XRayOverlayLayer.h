#pragma once

#include <ccHObject.h>
#include <ccGenericGLDisplay.h>

#include <QImage>

#include <memory>
#include <vector>

class ccPointCloud;
class QOpenGLTexture;

class XRayOverlayLayer : public ccHObject
{
public:
	enum class ColorMode
	{
		Monochrome = 0,
		HeightSpectral = 1,
	};

	explicit XRayOverlayLayer( ccPointCloud* cloud );
	~XRayOverlayLayer() override;

	CC_CLASS_ENUM getClassID() const override
	{
		return CC_TYPES::HIERARCHY_OBJECT;
	}

	bool isSerializable() const override
	{
		return false;
	}

	void setZRange( double zMin, double zMax );
	void setGamma( double gamma );
	void setInverted( bool inverted );
	void setColorMode( ColorMode mode );
	double gamma() const { return m_gamma; }
	bool inverted() const { return m_inverted; }
	ColorMode colorMode() const { return m_colorMode; }
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
	};

	void updateBounds();
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

	double m_zMin = 0.0;
	double m_zMax = 0.0;
	double m_gamma = 2.5;
	bool m_inverted = false;
	ColorMode m_colorMode = ColorMode::Monochrome;
};
