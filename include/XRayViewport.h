#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

class ccPointCloud;

class XRayViewport : public QWidget
{
	Q_OBJECT

public:
	explicit XRayViewport( QWidget* parent = nullptr );

	void setCloud( ccPointCloud* cloud );
	void setZRange( double zMin, double zMax );
	void setGamma( double gamma );
	void setInverted( bool inverted );
	void resetView();

protected:
	void paintEvent( QPaintEvent* event ) override;
	void resizeEvent( QResizeEvent* event ) override;
	void wheelEvent( QWheelEvent* event ) override;
	void mousePressEvent( QMouseEvent* event ) override;
	void mouseMoveEvent( QMouseEvent* event ) override;
	void mouseReleaseEvent( QMouseEvent* event ) override;

private:
	void updateCloudBounds();
	void renderXRay();
	void invalidateImage();
	void getCurrentView(double& x0, double& y1, double& viewW, double& viewH) const;

	ccPointCloud* m_cloud = nullptr;
	QImage m_image;

	double m_xMin = 0.0;
	double m_xMax = 1.0;
	double m_yMin = 0.0;
	double m_yMax = 1.0;
	double m_zMin = 0.0;
	double m_zMax = 0.0;
	double m_gamma = 0.75;
	double m_zoom = 1.0;
	double m_centerX = 0.0;
	double m_centerY = 0.0;
	bool m_inverted = false;
	bool m_dragging = false;
	QPoint m_lastMousePos;
};
