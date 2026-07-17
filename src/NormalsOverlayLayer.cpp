#include "../include/NormalsOverlayLayer.h"

#include <ccGLDrawContext.h>
#include <ccGenericPointCloud.h>
#include <ccIncludeGL.h>
#include <ccPointCloud.h>
#include <ccViewportParameters.h>

#include <QOpenGLTexture>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr int MaxOverlaySide = 1600;
	constexpr unsigned TargetOverlayPoints = 2000000;

	unsigned char colorChannel( double value )
	{
		return static_cast<unsigned char>( std::round( 255.0 * std::clamp( value, 0.0, 1.0 ) ) );
	}

	struct PaletteColors
	{
		double xr = 1.0;
		double xg = 0.0;
		double xb = 0.0;
		double yr = 0.0;
		double yg = 1.0;
		double yb = 0.0;
		double zr = 0.0;
		double zg = 0.0;
		double zb = 1.0;
	};

	PaletteColors paletteColors( NormalsOverlayLayer::Palette palette )
	{
		switch ( palette )
		{
		case NormalsOverlayLayer::Palette::SoftRGB:
			return { 0.90, 0.42, 0.44, 0.48, 0.79, 0.61, 0.43, 0.66, 1.00 };
		case NormalsOverlayLayer::Palette::BIM:
			return { 0.80, 0.36, 0.33, 0.38, 0.68, 0.52, 0.34, 0.57, 0.82 };
		case NormalsOverlayLayer::Palette::WarmCool:
			return { 0.88, 0.50, 0.38, 0.65, 0.72, 0.42, 0.50, 0.62, 0.90 };
		case NormalsOverlayLayer::Palette::RGB:
		default:
			return {};
		}
	}

	double densityBrightness( unsigned int count, double denom, double gamma )
	{
		if ( denom <= 0.0 )
		{
			return 1.0;
		}
		const double t = std::log1p( static_cast<double>( count ) ) / denom;
		return std::pow( std::clamp( t, 0.0, 1.0 ), std::max( 0.05, gamma ) );
	}

}

NormalsOverlayLayer::NormalsOverlayLayer( ccPointCloud* cloud )
	: ccHObject( cloud ? cloud->getName() + " - Normals Viewport Overlay" : QString( "Normals Viewport Overlay" ) )
	, m_cloud( cloud )
{
	setEnabled( true );
	setVisible( true );
	lockVisibility( false );
	updateCache();
}

NormalsOverlayLayer::~NormalsOverlayLayer() = default;

void NormalsOverlayLayer::setBackgroundGray( int value )
{
	m_backgroundGray = std::clamp( value, 0, 255 );
	invalidateImage();
}

void NormalsOverlayLayer::setPointRadius( int value )
{
	m_pointRadius = std::clamp( value, 1, 5 );
	invalidateImage();
}

void NormalsOverlayLayer::setDisplayMode( DisplayMode mode )
{
	m_displayMode = mode;
	invalidateImage();
}

void NormalsOverlayLayer::setPalette( Palette palette )
{
	m_palette = palette;
	invalidateImage();
}

void NormalsOverlayLayer::setGamma( double value )
{
	m_gamma = std::clamp( value, 0.10, 10.00 );
	invalidateImage();
}

void NormalsOverlayLayer::setFilterStrength( double value )
{
	m_filterStrength = std::clamp( value, 0.0, 1.0 );
	invalidateImage();
}

void NormalsOverlayLayer::setZRange( double zMin, double zMax )
{
	m_zMin = std::min( zMin, zMax );
	m_zMax = std::max( zMin, zMax );
	invalidateImage();
}

void NormalsOverlayLayer::updateCache()
{
	if ( !m_cloud || m_cloud->size() == 0 || !m_cloud->hasNormals() )
	{
		return;
	}

	const unsigned pointCount = m_cloud->size();
	const unsigned stride = pointCount > TargetOverlayPoints ? std::max( 1u, pointCount / TargetOverlayPoints ) : 1u;
	const bool hasVisibility = m_cloud->isVisibilityTableInstantiated();
	const ccGenericPointCloud::VisibilityTableType* visibility = hasVisibility ? &m_cloud->getTheVisibilityArray() : nullptr;
	m_points.clear();
	m_points.reserve( static_cast<size_t>( pointCount / stride ) + 1 );

	for ( unsigned i = 0; i < pointCount; i += stride )
	{
		if ( visibility && i < visibility->size() && ( *visibility )[i] != CCCoreLib::POINT_VISIBLE )
		{
			continue;
		}

		const CCVector3* p = m_cloud->getPointPersistentPtr( i );
		const CCVector3& n = m_cloud->getPointNormal( i );
		m_points.push_back( CachedPoint{ p->x, p->y, p->z, n.x, n.y, n.z } );
	}

	if ( m_points.empty() )
	{
		return;
	}

	m_visibilitySignature = visibilitySignature();

	m_zMin = m_points.front().z;
	m_zMax = m_points.front().z;
	for ( const CachedPoint& p : m_points )
	{
		m_zMin = std::min<double>( m_zMin, p.z );
		m_zMax = std::max<double>( m_zMax, p.z );
	}
}

ccBBox NormalsOverlayLayer::getOwnFitBB( ccGLMatrix& trans )
{
	if ( m_cloud )
	{
		return m_cloud->getOwnBB();
	}
	return ccBBox();
}

void NormalsOverlayLayer::drawMeOnly( CC_DRAW_CONTEXT& context )
{
	if ( !m_cloud || m_cloud->size() == 0 || m_points.empty() || !m_cloud->isBranchEnabled() )
	{
		return;
	}

	if ( !MACRO_Draw2D( context ) || !MACRO_Foreground( context ) || !context.display )
	{
		return;
	}

	ccGLCameraParameters camera;
	context.display->getGLCameraParameters( camera );

	const size_t currentVisibilitySignature = visibilitySignature();
	if ( currentVisibilitySignature != m_visibilitySignature )
	{
		updateCache();
		invalidateImage();
	}

	const bool cameraChanged = !m_hasLastCamera || !( camera == m_lastCamera );
	const bool sizeChanged = context.glW != m_lastW || context.glH != m_lastH;
	if ( m_image.isNull() || cameraChanged || sizeChanged )
	{
		renderImage( context, camera );
		m_lastCamera = camera;
		m_hasLastCamera = true;
		m_lastW = context.glW;
		m_lastH = context.glH;
	}

	if ( m_image.isNull() )
	{
		return;
	}

	QOpenGLFunctions_2_1* glFunc = context.glFunctions<QOpenGLFunctions_2_1>();
	if ( !glFunc || !ensureTexture() )
	{
		return;
	}

	glFunc->glPushAttrib( GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT );
	glFunc->glEnable( GL_BLEND );
	glFunc->glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glFunc->glEnable( GL_TEXTURE_2D );

	m_texture->bind();

	const float w = static_cast<float>( context.glW ) / 2.0f;
	const float h = static_cast<float>( context.glH ) / 2.0f;
	glFunc->glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glFunc->glBegin( GL_QUADS );
	glFunc->glTexCoord2f( 0, 1 );
	glFunc->glVertex2f( -w, -h );
	glFunc->glTexCoord2f( 1, 1 );
	glFunc->glVertex2f( w, -h );
	glFunc->glTexCoord2f( 1, 0 );
	glFunc->glVertex2f( w, h );
	glFunc->glTexCoord2f( 0, 0 );
	glFunc->glVertex2f( -w, h );
	glFunc->glEnd();

	m_texture->release();
	glFunc->glPopAttrib();
}

void NormalsOverlayLayer::invalidateImage()
{
	m_image = QImage();
	m_textureDirty = true;
}

bool NormalsOverlayLayer::ensureTexture()
{
	if ( m_image.isNull() )
	{
		return false;
	}

	if ( !m_texture || m_textureDirty )
	{
		m_texture.reset( new QOpenGLTexture( m_image ) );
		m_textureDirty = false;
	}

	return m_texture && m_texture->isCreated();
}

size_t NormalsOverlayLayer::visibilitySignature() const
{
	if ( !m_cloud || !m_cloud->isVisibilityTableInstantiated() )
	{
		return 0;
	}

	const ccGenericPointCloud::VisibilityTableType& visibility = m_cloud->getTheVisibilityArray();
	size_t hash = visibility.size();
	const size_t step = std::max<size_t>( 1, visibility.size() / 4096 );
	for ( size_t i = 0; i < visibility.size(); i += step )
	{
		hash = ( hash * 1315423911u ) ^ static_cast<size_t>( visibility[i] + 0x9e3779b9u + ( i << 6 ) + ( i >> 2 ) );
	}
	return hash;
}

void NormalsOverlayLayer::renderImage( CC_DRAW_CONTEXT& context, const ccGLCameraParameters& camera )
{
	const int viewW = context.glW;
	const int viewH = context.glH;
	if ( viewW <= 0 || viewH <= 0 || m_points.empty() )
	{
		m_image = QImage();
		m_textureDirty = true;
		return;
	}

	const int maxViewSide = std::max( viewW, viewH );
	const double imageScale = maxViewSide > MaxOverlaySide ? static_cast<double>( MaxOverlaySide ) / static_cast<double>( maxViewSide ) : 1.0;
	const int w = std::max( 1, static_cast<int>( std::round( static_cast<double>( viewW ) * imageScale ) ) );
	const int h = std::max( 1, static_cast<int>( std::round( static_cast<double>( viewH ) * imageScale ) ) );
	const size_t pixelCount = static_cast<size_t>( w ) * static_cast<size_t>( h );

	std::vector<unsigned int> counts( pixelCount, 0 );
	std::vector<double> rSums( pixelCount, 0.0 );
	std::vector<double> gSums( pixelCount, 0.0 );
	std::vector<double> bSums( pixelCount, 0.0 );

	CCVector3d viewDir = context.display->getViewportParameters().getViewDir();
	viewDir.normalize();

	const int pointRadius = std::clamp( m_pointRadius, 1, 5 );
	for ( const CachedPoint& p : m_points )
	{
		if ( p.z < m_zMin || p.z > m_zMax )
		{
			continue;
		}

		CCVector3d screen;
		bool inFrustum = false;
		const CCVector3 point( p.x, p.y, p.z );
		if ( camera.project( point, screen, &inFrustum ) && inFrustum )
		{
			const int ix = static_cast<int>( std::floor( screen.x * imageScale ) );
			const int iy = h - 1 - static_cast<int>( std::floor( screen.y * imageScale ) );
			if ( ix >= 0 && ix < w && iy >= 0 && iy < h )
			{
				for ( int dy = -pointRadius + 1; dy < pointRadius; ++dy )
				{
					const int py = iy + dy;
					if ( py < 0 || py >= h )
					{
						continue;
					}
					for ( int dx = -pointRadius + 1; dx < pointRadius; ++dx )
					{
						const int px = ix + dx;
						if ( px < 0 || px >= w )
						{
							continue;
						}

						double viewWeight = 1.0;
						if ( m_displayMode == DisplayMode::ViewFiltered )
						{
							const double facing = std::abs( static_cast<double>( p.nx ) * viewDir.x + static_cast<double>( p.ny ) * viewDir.y + static_cast<double>( p.nz ) * viewDir.z );
							const double sideVisibility = 1.0 - std::clamp( facing, 0.0, 1.0 );
							const double strongWeight = sideVisibility * sideVisibility;
							viewWeight = ( 1.0 - m_filterStrength ) + m_filterStrength * strongWeight;
						}

						const size_t pixelIndex = static_cast<size_t>( py ) * static_cast<size_t>( w ) + static_cast<size_t>( px );
						++counts[pixelIndex];
						rSums[pixelIndex] += std::abs( static_cast<double>( p.nx ) ) * viewWeight;
						gSums[pixelIndex] += std::abs( static_cast<double>( p.ny ) ) * viewWeight;
						bSums[pixelIndex] += std::abs( static_cast<double>( p.nz ) ) * viewWeight;
					}
				}
			}
		}
	}

	std::vector<unsigned int> nonZero;
	nonZero.reserve( counts.size() / 8 );
	for ( unsigned int count : counts )
	{
		if ( count > 0 )
		{
			nonZero.push_back( count );
		}
	}

	unsigned int limit = 1;
	if ( !nonZero.empty() )
	{
		const size_t idx = static_cast<size_t>( std::floor( 0.997 * static_cast<double>( nonZero.size() - 1 ) ) );
		std::nth_element( nonZero.begin(), nonZero.begin() + idx, nonZero.end() );
		limit = std::max( 1u, nonZero[idx] );
	}
	const double denom = std::log1p( static_cast<double>( limit ) );
	const PaletteColors palette = paletteColors( m_palette );

	m_image = QImage( w, h, QImage::Format_ARGB32 );
	m_textureDirty = true;
	const QRgb background = qRgba( 0, 0, 0, 0 );
	m_image.fill( background );

	for ( int y = 0; y < h; ++y )
	{
		QRgb* row = reinterpret_cast<QRgb*>( m_image.scanLine( y ) );
		for ( int x = 0; x < w; ++x )
		{
			const size_t pixelIndex = static_cast<size_t>( y ) * static_cast<size_t>( w ) + static_cast<size_t>( x );
			const unsigned int count = counts[pixelIndex];
			if ( count == 0 )
			{
				row[x] = background;
				continue;
			}

			const double invCount = 1.0 / static_cast<double>( count );
			const double brightness = densityBrightness( count, denom, m_gamma );
			const double nx = rSums[pixelIndex] * invCount;
			const double ny = gSums[pixelIndex] * invCount;
			const double nz = bSums[pixelIndex] * invCount;
			const double r = ( nx * palette.xr + ny * palette.yr + nz * palette.zr ) * brightness;
			const double g = ( nx * palette.xg + ny * palette.yg + nz * palette.zg ) * brightness;
			const double b = ( nx * palette.xb + ny * palette.yb + nz * palette.zb ) * brightness;
			const double alpha = std::clamp( std::max( { r, g, b } ), 0.0, 1.0 );
			row[x] = qRgba(
				colorChannel( r ),
				colorChannel( g ),
				colorChannel( b ),
				colorChannel( alpha ) );
		}
	}
}
