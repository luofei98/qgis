
#include "qgsmapsettings.h"

#include "qgsscalecalculator.h"
#include "qgsmaprendererjob.h"
#include "qgsmaptopixel.h"
#include "qgslogger.h"

#include "qgscrscache.h"
#include "qgsmessagelog.h"
#include "qgsmaplayer.h"
#include "qgsmaplayerregistry.h"
#include "qgsxmlutils.h"


QgsMapSettings::QgsMapSettings()
  : mDpi( 96 ) // what to set?
  , mSize( QSize( 0, 0 ) )
  , mExtent()
  , mProjectionsEnabled( false )
  , mDestCRS( GEOCRS_ID, QgsCoordinateReferenceSystem::InternalCrsId )  // WGS 84
  , mBackgroundColor( Qt::white )
  , mSelectionColor( Qt::yellow )
  , mFlags( Antialiasing | UseAdvancedEffects | DrawLabeling )
{
  updateDerived();

  // set default map units - we use WGS 84 thus use degrees
  setMapUnits( QGis::Degrees );
}


QgsRectangle QgsMapSettings::extent() const
{
  return mExtent;
}

void QgsMapSettings::setExtent(const QgsRectangle& extent)
{
  mExtent = extent;

  updateDerived();
}


void QgsMapSettings::updateDerived()
{
  QgsRectangle extent = mExtent;

  if ( extent.isEmpty() )
  {
    mValid = false;
    return;
  }

  // Don't allow zooms where the current extent is so small that it
  // can't be accurately represented using a double (which is what
  // currentExtent uses). Excluding 0 avoids a divide by zero and an
  // infinite loop when rendering to a new canvas. Excluding extents
  // greater than 1 avoids doing unnecessary calculations.

  // The scheme is to compare the width against the mean x coordinate
  // (and height against mean y coordinate) and only allow zooms where
  // the ratio indicates that there is more than about 12 significant
  // figures (there are about 16 significant figures in a double).

  if ( extent.width()  > 0 &&
       extent.height() > 0 &&
       extent.width()  < 1 &&
       extent.height() < 1 )
  {
    // Use abs() on the extent to avoid the case where the extent is
    // symmetrical about 0.
    double xMean = ( qAbs( extent.xMinimum() ) + qAbs( extent.xMaximum() ) ) * 0.5;
    double yMean = ( qAbs( extent.yMinimum() ) + qAbs( extent.yMaximum() ) ) * 0.5;

    double xRange = extent.width() / xMean;
    double yRange = extent.height() / yMean;

    static const double minProportion = 1e-12;
    if ( xRange < minProportion || yRange < minProportion )
    {
      mValid = false;
      return;
    }
  }

  double myHeight = mSize.height();
  double myWidth = mSize.width();

  if ( !myWidth || !myHeight )
  {
    mValid = false;
    return;
  }

  // calculate the translation and scaling parameters
  double mapUnitsPerPixelY = mExtent.height() / myHeight;
  double mapUnitsPerPixelX = mExtent.width() / myWidth;
  mMapUnitsPerPixel = mapUnitsPerPixelY > mapUnitsPerPixelX ? mapUnitsPerPixelY : mapUnitsPerPixelX;

  // calculate the actual extent of the mapCanvas
  double dxmin = mExtent.xMinimum(), dxmax = mExtent.xMaximum(),
         dymin = mExtent.yMinimum(), dymax = mExtent.yMaximum(), whitespace;

  if ( mapUnitsPerPixelY > mapUnitsPerPixelX )
  {
    whitespace = (( myWidth * mMapUnitsPerPixel ) - mExtent.width() ) * 0.5;
    dxmin -= whitespace;
    dxmax += whitespace;
  }
  else
  {
    whitespace = (( myHeight * mMapUnitsPerPixel ) - mExtent.height() ) * 0.5;
    dymin -= whitespace;
    dymax += whitespace;
  }

  mVisibleExtent.set( dxmin, dymin, dxmax, dymax );

  // update the scale
  mScaleCalculator.setDpi( mDpi );
  mScale = mScaleCalculator.calculate( mVisibleExtent, mSize.width() );

  mMapToPixel = QgsMapToPixel( mapUnitsPerPixel(), outputSize().height(), visibleExtent().yMinimum(), visibleExtent().xMinimum() );

  QgsDebugMsg( QString( "Map units per pixel (x,y) : %1, %2" ).arg( qgsDoubleToString( mapUnitsPerPixelX ) ).arg( qgsDoubleToString( mapUnitsPerPixelY ) ) );
  QgsDebugMsg( QString( "Pixmap dimensions (x,y) : %1, %2" ).arg( qgsDoubleToString( myWidth ) ).arg( qgsDoubleToString( myHeight ) ) );
  QgsDebugMsg( QString( "Extent dimensions (x,y) : %1, %2" ).arg( qgsDoubleToString( mExtent.width() ) ).arg( qgsDoubleToString( mExtent.height() ) ) );
  QgsDebugMsg( mExtent.toString() );
  QgsDebugMsg( QString( "Adjusted map units per pixel (x,y) : %1, %2" ).arg( qgsDoubleToString( mVisibleExtent.width() / myWidth ) ).arg( qgsDoubleToString( mVisibleExtent.height() / myHeight ) ) );
  QgsDebugMsg( QString( "Recalced pixmap dimensions (x,y) : %1, %2" ).arg( qgsDoubleToString( mVisibleExtent.width() / mMapUnitsPerPixel ) ).arg( qgsDoubleToString( mVisibleExtent.height() / mMapUnitsPerPixel ) ) );
  QgsDebugMsg( QString( "Scale (assuming meters as map units) = 1:%1" ).arg( qgsDoubleToString( mScale ) ) );

  mValid = true;
}


QSize QgsMapSettings::outputSize() const
{
  return mSize;
}

void QgsMapSettings::setOutputSize(const QSize& size)
{
  mSize = size;

  updateDerived();
}

int QgsMapSettings::outputDpi() const
{
  return mDpi;
}

void QgsMapSettings::setOutputDpi(int dpi)
{
  mDpi = dpi;

  updateDerived();
}


QStringList QgsMapSettings::layers() const
{
  return mLayers;
}

void QgsMapSettings::setLayers(const QStringList& layers)
{
  mLayers = layers;
}

void QgsMapSettings::setProjectionsEnabled( bool enabled )
{
  mProjectionsEnabled = enabled;
}

bool QgsMapSettings::hasCrsTransformEnabled() const
{
  return mProjectionsEnabled;
}


void QgsMapSettings::setDestinationCrs( const QgsCoordinateReferenceSystem& crs )
{
  mDestCRS = crs;
}

const QgsCoordinateReferenceSystem& QgsMapSettings::destinationCrs() const
{
  return mDestCRS;
}


void QgsMapSettings::setMapUnits( QGis::UnitType u )
{
  mScaleCalculator.setMapUnits( u );

  // Since the map units have changed, force a recalculation of the scale.
  updateDerived();
}

void QgsMapSettings::setFlags( QgsMapSettings::Flags flags )
{
  mFlags = flags;
}

void QgsMapSettings::setFlag( QgsMapSettings::Flag flag, bool on )
{
  if ( on )
    mFlags |= flag;
  else
    mFlags &= ~flag;
}

QgsMapSettings::Flags QgsMapSettings::flags() const
{
  return mFlags;
}

bool QgsMapSettings::testFlag( QgsMapSettings::Flag flag ) const
{
  return mFlags.testFlag( flag );
}

QGis::UnitType QgsMapSettings::mapUnits() const
{
  return mScaleCalculator.mapUnits();
}


bool QgsMapSettings::hasValidSettings() const
{
  return mValid;
}

QgsRectangle QgsMapSettings::visibleExtent() const
{
  return mVisibleExtent;
}

double QgsMapSettings::mapUnitsPerPixel() const
{
  return mMapUnitsPerPixel;
}

double QgsMapSettings::scale() const
{
  return mScale;
}





const QgsCoordinateTransform* QgsMapSettings::coordTransform( QgsMapLayer *layer ) const
{
  if ( !layer )
  {
    return 0;
  }
  return QgsCoordinateTransformCache::instance()->transform( layer->crs().authid(), mDestCRS.authid() );
}



QgsRectangle QgsMapSettings::layerExtentToOutputExtent( QgsMapLayer* theLayer, QgsRectangle extent ) const
{
  QgsDebugMsg( QString( "sourceCrs = " + coordTransform( theLayer )->sourceCrs().authid() ) );
  QgsDebugMsg( QString( "destCRS = " + coordTransform( theLayer )->destCRS().authid() ) );
  QgsDebugMsg( QString( "extent = " + extent.toString() ) );
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      extent = coordTransform( theLayer )->transformBoundingBox( extent );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }

  QgsDebugMsg( QString( "proj extent = " + extent.toString() ) );

  return extent;
}


QgsRectangle QgsMapSettings::outputExtentToLayerExtent( QgsMapLayer* theLayer, QgsRectangle extent ) const
{
  QgsDebugMsg( QString( "layer sourceCrs = " + coordTransform( theLayer )->sourceCrs().authid() ) );
  QgsDebugMsg( QString( "layer destCRS = " + coordTransform( theLayer )->destCRS().authid() ) );
  QgsDebugMsg( QString( "extent = " + extent.toString() ) );
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      extent = coordTransform( theLayer )->transformBoundingBox( extent, QgsCoordinateTransform::ReverseTransform );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }

  QgsDebugMsg( QString( "proj extent = " + extent.toString() ) );

  return extent;
}


QgsPoint QgsMapSettings::layerToMapCoordinates( QgsMapLayer* theLayer, QgsPoint point ) const
{
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      point = coordTransform( theLayer )->transform( point, QgsCoordinateTransform::ForwardTransform );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }
  else
  {
    // leave point without transformation
  }
  return point;
}


QgsRectangle QgsMapSettings::layerToMapCoordinates( QgsMapLayer* theLayer, QgsRectangle rect ) const
{
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      rect = coordTransform( theLayer )->transform( rect, QgsCoordinateTransform::ForwardTransform );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }
  else
  {
    // leave point without transformation
  }
  return rect;
}


QgsPoint QgsMapSettings::mapToLayerCoordinates( QgsMapLayer* theLayer, QgsPoint point ) const
{
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      point = coordTransform( theLayer )->transform( point, QgsCoordinateTransform::ReverseTransform );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }
  else
  {
    // leave point without transformation
  }
  return point;
}


QgsRectangle QgsMapSettings::mapToLayerCoordinates( QgsMapLayer* theLayer, QgsRectangle rect ) const
{
  if ( hasCrsTransformEnabled() )
  {
    try
    {
      rect = coordTransform( theLayer )->transform( rect, QgsCoordinateTransform::ReverseTransform );
    }
    catch ( QgsCsException &cse )
    {
      QgsMessageLog::logMessage( QString( "Transform error caught: %1" ).arg( cse.what() ), "CRS" );
    }
  }
  return rect;
}



QgsRectangle QgsMapSettings::fullExtent() const
{
  QgsDebugMsg( "called." );
  QgsMapLayerRegistry* registry = QgsMapLayerRegistry::instance();

  // reset the map canvas extent since the extent may now be smaller
  // We can't use a constructor since QgsRectangle normalizes the rectangle upon construction
  QgsRectangle fullExtent;
  fullExtent.setMinimal();

  // iterate through the map layers and test each layers extent
  // against the current min and max values
  QStringList::const_iterator it = mLayers.begin();
  QgsDebugMsg( QString( "Layer count: %1" ).arg( mLayers.count() ) );
  while ( it != mLayers.end() )
  {
    QgsMapLayer * lyr = registry->mapLayer( *it );
    if ( lyr == NULL )
    {
      QgsDebugMsg( QString( "WARNING: layer '%1' not found in map layer registry!" ).arg( *it ) );
    }
    else
    {
      QgsDebugMsg( "Updating extent using " + lyr->name() );
      QgsDebugMsg( "Input extent: " + lyr->extent().toString() );

      if ( lyr->extent().isEmpty() )
      {
        it++;
        continue;
      }

      // Layer extents are stored in the coordinate system (CS) of the
      // layer. The extent must be projected to the canvas CS
      QgsRectangle extent = layerExtentToOutputExtent( lyr, lyr->extent() );

      QgsDebugMsg( "Output extent: " + extent.toString() );
      fullExtent.unionRect( extent );

    }
    it++;
  }

  if ( fullExtent.width() == 0.0 || fullExtent.height() == 0.0 )
  {
    // If all of the features are at the one point, buffer the
    // rectangle a bit. If they are all at zero, do something a bit
    // more crude.

    if ( fullExtent.xMinimum() == 0.0 && fullExtent.xMaximum() == 0.0 &&
         fullExtent.yMinimum() == 0.0 && fullExtent.yMaximum() == 0.0 )
    {
      fullExtent.set( -1.0, -1.0, 1.0, 1.0 );
    }
    else
    {
      const double padFactor = 1e-8;
      double widthPad = fullExtent.xMinimum() * padFactor;
      double heightPad = fullExtent.yMinimum() * padFactor;
      double xmin = fullExtent.xMinimum() - widthPad;
      double xmax = fullExtent.xMaximum() + widthPad;
      double ymin = fullExtent.yMinimum() - heightPad;
      double ymax = fullExtent.yMaximum() + heightPad;
      fullExtent.set( xmin, ymin, xmax, ymax );
    }
  }

  QgsDebugMsg( "Full extent: " + fullExtent.toString() );
  return fullExtent;
}


void QgsMapSettings::readXML( QDomNode& theNode )
{
  // set units
  QDomNode mapUnitsNode = theNode.namedItem( "units" );
  QGis::UnitType units = QgsXmlUtils::readMapUnits( mapUnitsNode.toElement() );
  setMapUnits( units );

  // set projections flag
  QDomNode projNode = theNode.namedItem( "projections" );
  setProjectionsEnabled( projNode.toElement().text().toInt() );

  // set destination CRS
  QgsCoordinateReferenceSystem srs;
  QDomNode srsNode = theNode.namedItem( "destinationsrs" );
  srs.readXML( srsNode );
  setDestinationCrs( srs );

  // set extent
  QDomNode extentNode = theNode.namedItem( "extent" );
  QgsRectangle aoi = QgsXmlUtils::readRectangle( extentNode.toElement() );
  setExtent( aoi );
}



void QgsMapSettings::writeXML( QDomNode& theNode, QDomDocument& theDoc )
{
  // units
  theNode.appendChild( QgsXmlUtils::writeMapUnits( mapUnits(), theDoc ) );

  // Write current view extents
  theNode.appendChild( QgsXmlUtils::writeRectangle( extent(), theDoc ) );

  // projections enabled
  QDomElement projNode = theDoc.createElement( "projections" );
  projNode.appendChild( theDoc.createTextNode( QString::number( hasCrsTransformEnabled() ) ) );
  theNode.appendChild( projNode );

  // destination CRS
  QDomElement srsNode = theDoc.createElement( "destinationsrs" );
  theNode.appendChild( srsNode );
  destinationCrs().writeXML( srsNode, theDoc );
}