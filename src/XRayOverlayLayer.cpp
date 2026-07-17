#include "../include/XRayOverlayLayer.h"

#include <ccGLDrawContext.h>
#include <ccGenericPointCloud.h>
#include <ccIncludeGL.h>
#include <ccPointCloud.h>

#include <QOpenGLTexture>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr int MaxOverlaySide = 1600;
	constexpr unsigned TargetOverlayPoints = 2000000;

	QRgb spectralColor( double t, double brightness )
	{
		t = std::clamp( t, 0.0, 1.0 );
		brightness = std::clamp( brightness, 0.0, 1.0 );

		const double stops[][3] = {
			{ 0.0, 70.0, 255.0 },
			{ 0.0, 190.0, 255.0 },
			{ 0.0, 220.0, 80.0 },
			{ 255.0, 230.0, 0.0 },
			{ 255.0, 95.0, 0.0 },
			{ 255.0, 0.0, 0.0 },
		};
		constexpr int stopCount = static_cast<int>( sizeof( stops ) / sizeof( stops[0] ) );
		const double scaled = t * static_cast<double>( stopCount - 1 );
		const int idx = std::clamp( static_cast<int>( std::floor( scaled ) ), 0, stopCount - 2 );
		const double u = scaled - static_cast<double>( idx );

		const int r = static_cast<int>( std::round( ( stops[idx][0] * ( 1.0 - u ) + stops[idx + 1][0] * u ) * brightness ) );
		const int g = static_cast<int>( std::round( ( stops[idx][1] * ( 1.0 - u ) + stops[idx + 1][1] * u ) * brightness ) );
		const int b = static_cast<int>( std::round( ( stops[idx][2] * ( 1.0 - u ) + stops[idx + 1][2] * u ) * brightness ) );
		return qRgb( std::clamp( r, 0, 255 ), std::clamp( g, 0, 255 ), std::clamp( b, 0, 255 ) );
	}
}

XRayOverlayLayer::XRayOverlayLayer( ccPointCloud* cloud )
	: ccHObject( cloud ? cloud->getName() + " - X-Ray Viewport Overlay" : QString( "X-Ray Viewport Overlay" ) )
	, m_cloud( cloud )
{
	setEnabled( true );
	setVisible( true );
	lockVisibility( false );
	updateBounds();
}

XRayOverlayLayer::~XRayOverlayLayer() = default;

void XRayOverlayLayer::setZRange( double zMin, double zMax )
{
	m_zMin = std::min( zMin, zMax );
	m_zMax = std::max( zMin, zMax );
	invalidateImage();
}

void XRayOverlayLayer::setGamma( double gamma )
{
	m_gamma = std::max( 0.05, gamma );
	invalidateImage();
}

void XRayOverlayLayer::setInverted( bool inverted )
{
	m_inverted = inverted;
	invalidateImage();
}

void XRayOverlayLayer::setColorMode( ColorMode mode )
{
	m_colorMode = mode;
	invalidateImage();
}

void XRayOverlayLayer::updateBounds()
{
	if ( !m_cloud || m_cloud->size() == 0 || !m_cloud->isBranchEnabled() )
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
		m_points.push_back( CachedPoint{ p->x, p->y, p->z } );
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

ccBBox XRayOverlayLayer::getOwnFitBB( ccGLMatrix& trans )
{
	if ( m_cloud )
	{
		return m_cloud->getOwnBB();
	}
	return ccBBox();
}

void XRayOverlayLayer::drawMeOnly( CC_DRAW_CONTEXT& context )
{
	if ( !m_cloud || m_cloud->size() == 0 || !m_cloud->isBranchEnabled() )
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
		updateBounds();
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

void XRayOverlayLayer::invalidateImage()
{
	m_image = QImage();
	m_textureDirty = true;
}

bool XRayOverlayLayer::ensureTexture()
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

size_t XRayOverlayLayer::visibilitySignature() const
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

void XRayOverlayLayer::renderImage( CC_DRAW_CONTEXT& context, const ccGLCameraParameters& camera )
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

	std::vector<unsigned int> counts( static_cast<size_t>( w ) * static_cast<size_t>( h ), 0 );
	std::vector<double> zSums;
	if ( m_colorMode == ColorMode::HeightSpectral )
	{
		zSums.resize( counts.size(), 0.0 );
	}

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
				const size_t pixelIndex = static_cast<size_t>( iy ) * static_cast<size_t>( w ) + static_cast<size_t>( ix );
				++counts[pixelIndex];
				if ( !zSums.empty() )
				{
					zSums[pixelIndex] += static_cast<double>( p.z );
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

	m_image = QImage( w, h, QImage::Format_ARGB32 );
	m_textureDirty = true;
	const QRgb background = qRgba( 0, 0, 0, 0 );
	m_image.fill( background );

	const double denom = std::log1p( static_cast<double>( limit ) );
	const double visibleZSpan = std::max( 1e-9, m_zMax - m_zMin );
	for ( int y = 0; y < h; ++y )
	{
		QRgb* row = reinterpret_cast<QRgb*>( m_image.scanLine( y ) );
		for ( int x = 0; x < w; ++x )
		{
			const unsigned int count = counts[static_cast<size_t>( y ) * static_cast<size_t>( w ) + static_cast<size_t>( x )];
			if ( count == 0 )
			{
				row[x] = background;
				continue;
			}

			const double density = std::clamp( std::log1p( static_cast<double>( count ) ) / denom, 0.0, 1.0 );
			const double opacity = std::pow( density, 0.5 * std::max( 0.05, m_gamma ) );
			if ( m_colorMode == ColorMode::HeightSpectral )
			{
				const size_t pixelIndex = static_cast<size_t>( y ) * static_cast<size_t>( w ) + static_cast<size_t>( x );
				const double avgZ = zSums[pixelIndex] / static_cast<double>( count );
				const double zT = ( avgZ - m_zMin ) / visibleZSpan;
				const QRgb color = spectralColor( zT, 1.0 );
				const int alpha = static_cast<int>( std::round( 255.0 * opacity ) );
				row[x] = qRgba( qRed( color ), qGreen( color ), qBlue( color ), std::clamp( alpha, 0, 255 ) );
			}
			else
			{
				const int value = m_inverted ? 255 : 0;
				const int alpha = static_cast<int>( std::round( 255.0 * opacity ) );
				row[x] = qRgba( value, value, value, std::clamp( alpha, 0, 255 ) );
			}
		}
	}
}
