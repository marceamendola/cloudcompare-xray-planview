#include "../include/XRayViewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

#include <ccPointCloud.h>

XRayViewport::XRayViewport( QWidget* parent )
	: QWidget( parent )
{
	setMinimumSize( 640, 480 );
	setMouseTracking( true );
	setAutoFillBackground( false );
}

void XRayViewport::setCloud( ccPointCloud* cloud )
{
	m_cloud = cloud;
	updateCloudBounds();
	resetView();
}

void XRayViewport::setZRange( double zMin, double zMax )
{
	m_zMin = zMin;
	m_zMax = zMax;
	invalidateImage();
}

void XRayViewport::setGamma( double gamma )
{
	m_gamma = std::max( 0.05, gamma );
	invalidateImage();
}

void XRayViewport::setInverted( bool inverted )
{
	m_inverted = inverted;
	invalidateImage();
}

void XRayViewport::resetView()
{
	m_zoom = 1.0;
	m_centerX = 0.5 * ( m_xMin + m_xMax );
	m_centerY = 0.5 * ( m_yMin + m_yMax );
	invalidateImage();
}

void XRayViewport::paintEvent( QPaintEvent* )
{
	if ( m_image.isNull() )
	{
		renderXRay();
	}

	QPainter painter( this );
	painter.fillRect( rect(), m_inverted ? Qt::black : Qt::white );
	if ( !m_image.isNull() )
	{
		painter.drawImage( rect(), m_image );
	}
}

void XRayViewport::resizeEvent( QResizeEvent* )
{
	invalidateImage();
}

void XRayViewport::wheelEvent( QWheelEvent* event )
{
	const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
	double oldX0 = 0.0;
	double oldY1 = 0.0;
	double oldViewW = 1.0;
	double oldViewH = 1.0;
	getCurrentView( oldX0, oldY1, oldViewW, oldViewH );

	const QPointF mousePos = event->position();
	const double mouseWorldX = oldX0 + ( mousePos.x() / std::max( 1, width() ) ) * oldViewW;
	const double mouseWorldY = oldY1 - ( mousePos.y() / std::max( 1, height() ) ) * oldViewH;
	const double rx = mousePos.x() / std::max( 1, width() ) - 0.5;
	const double ry = 0.5 - mousePos.y() / std::max( 1, height() );

	m_zoom = std::clamp( m_zoom * factor, 0.05, 200.0 );

	double newX0 = 0.0;
	double newY1 = 0.0;
	double newViewW = 1.0;
	double newViewH = 1.0;
	getCurrentView( newX0, newY1, newViewW, newViewH );
	m_centerX = mouseWorldX - rx * newViewW;
	m_centerY = mouseWorldY - ry * newViewH;

	invalidateImage();
}

void XRayViewport::mousePressEvent( QMouseEvent* event )
{
	if ( event->button() == Qt::LeftButton )
	{
		m_dragging = true;
		m_lastMousePos = event->pos();
	}
}

void XRayViewport::mouseMoveEvent( QMouseEvent* event )
{
	if ( !m_dragging )
	{
		return;
	}

	const QPoint delta = event->pos() - m_lastMousePos;
	m_lastMousePos = event->pos();

	double x0 = 0.0;
	double y1 = 0.0;
	double viewW = 1.0;
	double viewH = 1.0;
	getCurrentView( x0, y1, viewW, viewH );
	m_centerX -= static_cast<double>( delta.x() ) / std::max( 1, width() ) * viewW;
	m_centerY += static_cast<double>( delta.y() ) / std::max( 1, height() ) * viewH;
	invalidateImage();
}

void XRayViewport::mouseReleaseEvent( QMouseEvent* event )
{
	if ( event->button() == Qt::LeftButton )
	{
		m_dragging = false;
	}
}

void XRayViewport::updateCloudBounds()
{
	if ( !m_cloud || m_cloud->size() == 0 )
	{
		return;
	}

	const CCVector3* p0 = m_cloud->getPointPersistentPtr( 0 );
	m_xMin = m_xMax = p0->x;
	m_yMin = m_yMax = p0->y;
	m_zMin = m_zMax = p0->z;

	const unsigned pointCount = m_cloud->size();
	const unsigned stride = pointCount > 10000000 ? pointCount / 10000000 : 1;
	for ( unsigned i = 0; i < pointCount; i += stride )
	{
		const CCVector3* p = m_cloud->getPointPersistentPtr( i );
		m_xMin = std::min<double>( m_xMin, p->x );
		m_xMax = std::max<double>( m_xMax, p->x );
		m_yMin = std::min<double>( m_yMin, p->y );
		m_yMax = std::max<double>( m_yMax, p->y );
		m_zMin = std::min<double>( m_zMin, p->z );
		m_zMax = std::max<double>( m_zMax, p->z );
	}
}

void XRayViewport::renderXRay()
{
	const int w = width();
	const int h = height();
	if ( !m_cloud || m_cloud->size() == 0 || w <= 0 || h <= 0 )
	{
		m_image = QImage();
		return;
	}

	std::vector<unsigned int> counts( static_cast<size_t>( w ) * static_cast<size_t>( h ), 0 );

	double x0 = 0.0;
	double y1 = 0.0;
	double viewW = 1.0;
	double viewH = 1.0;
	getCurrentView( x0, y1, viewW, viewH );
	const double worldPerPixelX = viewW / static_cast<double>( w );
	const double worldPerPixelY = viewH / static_cast<double>( h );

	const unsigned pointCount = m_cloud->size();
	const unsigned targetPreviewPoints = m_dragging ? 1500000 : 8000000;
	const unsigned stride = pointCount > targetPreviewPoints ? std::max( 1u, pointCount / targetPreviewPoints ) : 1u;

	for ( unsigned i = 0; i < pointCount; i += stride )
	{
		const CCVector3* p = m_cloud->getPointPersistentPtr( i );
		if ( p->z < m_zMin || p->z > m_zMax )
		{
			continue;
		}

		const int ix = static_cast<int>( std::floor( ( p->x - x0 ) / worldPerPixelX ) );
		const int iy = static_cast<int>( std::floor( ( y1 - p->y ) / worldPerPixelY ) );
		if ( ix >= 0 && ix < w && iy >= 0 && iy < h )
		{
			++counts[static_cast<size_t>( iy ) * static_cast<size_t>( w ) + static_cast<size_t>( ix )];
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

	m_image = QImage( w, h, QImage::Format_RGB32 );
	const QRgb background = m_inverted ? qRgb( 0, 0, 0 ) : qRgb( 255, 255, 255 );
	m_image.fill( background );

	const double denom = std::log1p( static_cast<double>( limit ) );
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

			double t = std::log1p( static_cast<double>( count ) ) / denom;
			t = std::pow( std::clamp( t, 0.0, 1.0 ), m_gamma );
			const int value = static_cast<int>( std::round( 255.0 * ( m_inverted ? t : ( 1.0 - t ) ) ) );
			row[x] = qRgb( value, value, value );
		}
	}
}

void XRayViewport::invalidateImage()
{
	m_image = QImage();
	update();
}

void XRayViewport::getCurrentView(double& x0, double& y1, double& viewW, double& viewH) const
{
	const double spanX = std::max( 1e-9, ( m_xMax - m_xMin ) / m_zoom );
	const double spanY = std::max( 1e-9, ( m_yMax - m_yMin ) / m_zoom );
	const double viewAspect = static_cast<double>( std::max( 1, width() ) ) / static_cast<double>( std::max( 1, height() ) );
	viewW = spanX;
	viewH = spanY;
	if ( viewW / viewH > viewAspect )
	{
		viewH = viewW / viewAspect;
	}
	else
	{
		viewW = viewH * viewAspect;
	}

	x0 = m_centerX - 0.5 * viewW;
	y1 = m_centerY + 0.5 * viewH;
}
