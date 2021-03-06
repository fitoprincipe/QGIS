/***************************************************************************
                         testqgsprocessing.cpp
                         ---------------------
    begin                : January 2017
    copyright            : (C) 2017 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsprocessingregistry.h"
#include "qgsprocessingprovider.h"
#include "qgsprocessingutils.h"
#include "qgsprocessingalgorithm.h"
#include "qgsprocessingcontext.h"
#include "qgsprocessingmodelalgorithm.h"
#include <QObject>
#include <QtTest/QSignalSpy>
#include "qgis.h"
#include "qgstest.h"
#include "qgstestutils.h"
#include "qgsrasterlayer.h"
#include "qgsproject.h"
#include "qgspoint.h"
#include "qgsgeometry.h"
#include "qgsvectorfilewriter.h"
#include "qgsexpressioncontext.h"
#include "qgsxmlutils.h"

class DummyAlgorithm : public QgsProcessingAlgorithm
{
  public:

    DummyAlgorithm( const QString &name ) : mName( name ) { mFlags = QgsProcessingAlgorithm::flags(); }

    QString name() const override { return mName; }
    QString displayName() const override { return mName; }
    virtual QVariantMap processAlgorithm( const QVariantMap &,
                                          QgsProcessingContext &, QgsProcessingFeedback * ) const override { return QVariantMap(); }

    virtual Flags flags() const override { return mFlags; }

    QString mName;

    Flags mFlags;

    void checkParameterVals()
    {
      addParameter( new QgsProcessingParameterString( "p1" ) );
      QVariantMap params;
      QgsProcessingContext context;

      QVERIFY( !checkParameterValues( params, context ) );
      params.insert( "p1", "a" );
      QVERIFY( checkParameterValues( params, context ) );
      // optional param
      addParameter( new QgsProcessingParameterString( "p2", QString(), QVariant(), false, true ) );
      QVERIFY( checkParameterValues( params, context ) );
      params.insert( "p2", "a" );
      QVERIFY( checkParameterValues( params, context ) );
    }

    void runParameterChecks()
    {
      QVERIFY( parameterDefinitions().isEmpty() );
      QVERIFY( addParameter( new QgsProcessingParameterBoolean( "p1" ) ) );
      QCOMPARE( parameterDefinitions().count(), 1 );
      QCOMPARE( parameterDefinitions().at( 0 )->name(), QString( "p1" ) );

      QVERIFY( !addParameter( nullptr ) );
      QCOMPARE( parameterDefinitions().count(), 1 );
      // duplicate name!
      QgsProcessingParameterBoolean *p2 = new QgsProcessingParameterBoolean( "p1" );
      QVERIFY( !addParameter( p2 ) );
      delete p2;
      QCOMPARE( parameterDefinitions().count(), 1 );

      QCOMPARE( parameterDefinition( "p1" ), parameterDefinitions().at( 0 ) );
      // parameterDefinition should be case insensitive
      QCOMPARE( parameterDefinition( "P1" ), parameterDefinitions().at( 0 ) );
      QVERIFY( !parameterDefinition( "invalid" ) );

      QCOMPARE( countVisibleParameters(), 1 );
      QgsProcessingParameterBoolean *p3 = new QgsProcessingParameterBoolean( "p3" );
      QVERIFY( addParameter( p3 ) );
      QCOMPARE( countVisibleParameters(), 2 );
      QgsProcessingParameterBoolean *p4 = new QgsProcessingParameterBoolean( "p4" );
      p4->setFlags( QgsProcessingParameterDefinition::FlagHidden );
      QVERIFY( addParameter( p4 ) );
      QCOMPARE( countVisibleParameters(), 2 );


      //destination styleparameters
      QVERIFY( destinationParameterDefinitions().isEmpty() );
      QgsProcessingParameterFeatureSink *p5 = new QgsProcessingParameterFeatureSink( "p5" );
      QVERIFY( addParameter( p5 ) );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 );
      QgsProcessingParameterFeatureSink *p6 = new QgsProcessingParameterFeatureSink( "p6" );
      QVERIFY( addParameter( p6 ) );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 << p6 );

      // remove parameter
      removeParameter( "non existent" );
      removeParameter( "p6" );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 );
      removeParameter( "p5" );
      QVERIFY( destinationParameterDefinitions().isEmpty() );
    }

    void runOutputChecks()
    {
      QVERIFY( outputDefinitions().isEmpty() );
      QVERIFY( addOutput( new QgsProcessingOutputVectorLayer( "p1" ) ) );
      QCOMPARE( outputDefinitions().count(), 1 );
      QCOMPARE( outputDefinitions().at( 0 )->name(), QString( "p1" ) );

      QVERIFY( !addOutput( nullptr ) );
      QCOMPARE( outputDefinitions().count(), 1 );
      // duplicate name!
      QgsProcessingOutputVectorLayer *p2 = new QgsProcessingOutputVectorLayer( "p1" );
      QVERIFY( !addOutput( p2 ) );
      delete p2;
      QCOMPARE( outputDefinitions().count(), 1 );

      QCOMPARE( outputDefinition( "p1" ), outputDefinitions().at( 0 ) );
      // parameterDefinition should be case insensitive
      QCOMPARE( outputDefinition( "P1" ), outputDefinitions().at( 0 ) );
      QVERIFY( !outputDefinition( "invalid" ) );

      QVERIFY( !hasHtmlOutputs() );
      QgsProcessingOutputHtml *p3 = new QgsProcessingOutputHtml( "p3" );
      QVERIFY( addOutput( p3 ) );
      QVERIFY( hasHtmlOutputs() );
    }

    void runValidateInputCrsChecks()
    {
      addParameter( new QgsProcessingParameterMapLayer( "p1" ) );
      addParameter( new QgsProcessingParameterMapLayer( "p2" ) );
      QVariantMap parameters;

      QgsVectorLayer *layer3111 = new QgsVectorLayer( "Point?crs=epsg:3111", "v1", "memory" );
      QgsProject p;
      p.addMapLayer( layer3111 );

      QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
      QString raster1 = testDataDir + "tenbytenraster.asc";
      QFileInfo fi1( raster1 );
      QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
      QVERIFY( r1->isValid() );
      p.addMapLayer( r1 );

      QgsVectorLayer *layer4326 = new QgsVectorLayer( "Point?crs=epsg:4326", "v1", "memory" );
      p.addMapLayer( layer4326 );

      QgsProcessingContext context;
      context.setProject( &p );

      // flag not set
      mFlags = 0;
      parameters.insert( "p1", QVariant::fromValue( layer3111 ) );
      QVERIFY( validateInputCrs( parameters, context ) );
      mFlags = FlagRequiresMatchingCrs;
      QVERIFY( validateInputCrs( parameters, context ) );

      // two layers, different crs
      parameters.insert( "p2", QVariant::fromValue( layer4326 ) );
      // flag not set
      mFlags = 0;
      QVERIFY( validateInputCrs( parameters, context ) );
      mFlags = FlagRequiresMatchingCrs;
      QVERIFY( !validateInputCrs( parameters, context ) );

      // raster layer
      parameters.remove( "p2" );
      addParameter( new QgsProcessingParameterRasterLayer( "p3" ) );
      parameters.insert( "p3", QVariant::fromValue( r1 ) );
      QVERIFY( !validateInputCrs( parameters, context ) );

      // feature source
      parameters.remove( "p3" );
      addParameter( new QgsProcessingParameterFeatureSource( "p4" ) );
      parameters.insert( "p4", layer4326->id() );
      QVERIFY( !validateInputCrs( parameters, context ) );

      parameters.remove( "p4" );
      addParameter( new QgsProcessingParameterMultipleLayers( "p5" ) );
      parameters.insert( "p5", QVariantList() << layer4326->id() << r1->id() );
      QVERIFY( !validateInputCrs( parameters, context ) );
    }

    void runAsPythonCommandChecks()
    {
      addParameter( new QgsProcessingParameterString( "p1" ) );
      addParameter( new QgsProcessingParameterString( "p2" ) );
      QgsProcessingParameterString *hidden = new QgsProcessingParameterString( "p3" );
      hidden->setFlags( QgsProcessingParameterDefinition::FlagHidden );
      addParameter( hidden );

      QVariantMap params;
      QgsProcessingContext context;

      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {})" ) );
      params.insert( "p1", "a" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a'})" ) );
      params.insert( "p2", QVariant() );
      // not set, should be no change
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a'})" ) );
      params.insert( "p2", "b" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a','p2':'b'})" ) );

      // hidden, shouldn't be shown
      params.insert( "p3", "b" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a','p2':'b'})" ) );
    }

};

//dummy provider for testing
class DummyProvider : public QgsProcessingProvider
{
  public:

    DummyProvider( const QString &id ) : mId( id ) {}

    virtual QString id() const override { return mId; }

    virtual QString name() const override { return "dummy"; }

    void unload() override { if ( unloaded ) { *unloaded = true; } }

    bool *unloaded = nullptr;

  protected:

    virtual void loadAlgorithms() override
    {
      QVERIFY( addAlgorithm( new DummyAlgorithm( "alg1" ) ) );
      QVERIFY( addAlgorithm( new DummyAlgorithm( "alg2" ) ) );

      //dupe name
      QgsProcessingAlgorithm *a = new DummyAlgorithm( "alg1" );
      QVERIFY( !addAlgorithm( a ) );
      delete a;

      QVERIFY( !addAlgorithm( nullptr ) );
    }

    QString mId;


};

class DummyProviderNoLoad : public DummyProvider
{
  public:

    DummyProviderNoLoad( const QString &id ) : DummyProvider( id ) {}

    bool load() override
    {
      return false;
    }

};

class TestQgsProcessing: public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase(); // will be called after the last testfunction was executed.
    void init() {} // will be called before each testfunction is executed.
    void cleanup() {} // will be called after every testfunction.
    void instance();
    void addProvider();
    void providerById();
    void removeProvider();
    void compatibleLayers();
    void normalizeLayerSource();
    void context();
    void mapLayers();
    void mapLayerFromStore();
    void mapLayerFromString();
    void algorithm();
    void features();
    void uniqueValues();
    void createIndex();
    void createFeatureSink();
    void parameters();
    void algorithmParameters();
    void algorithmOutputs();
    void parameterGeneral();
    void parameterBoolean();
    void parameterCrs();
    void parameterLayer();
    void parameterExtent();
    void parameterPoint();
    void parameterFile();
    void parameterMatrix();
    void parameterLayerList();
    void parameterNumber();
    void parameterRange();
    void parameterRasterLayer();
    void parameterEnum();
    void parameterString();
    void parameterExpression();
    void parameterField();
    void parameterVectorLayer();
    void parameterFeatureSource();
    void parameterFeatureSink();
    void parameterVectorOut();
    void parameterRasterOut();
    void parameterFileOut();
    void parameterFolderOut();
    void checkParamValues();
    void combineLayerExtent();
    void processingFeatureSource();
    void processingFeatureSink();
    void algorithmScope();
    void validateInputCrs();
    void generateIteratingDestination();
    void asPythonCommand();
    void modelerAlgorithm();
    void modelExecution();
    void tempUtils();

  private:

};

void TestQgsProcessing::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();

  // Set up the QgsSettings environment
  QCoreApplication::setOrganizationName( QStringLiteral( "QGIS" ) );
  QCoreApplication::setOrganizationDomain( QStringLiteral( "qgis.org" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "QGIS-TEST" ) );
}

void TestQgsProcessing::cleanupTestCase()
{
  QFile::remove( QDir::tempPath() + "/create_feature_sink.tab" );
  QgsVectorFileWriter::deleteShapeFile( QDir::tempPath() + "/create_feature_sink2.shp" );

  QgsApplication::exitQgis();
}

void TestQgsProcessing::instance()
{
  // test that application has a registry instance
  QVERIFY( QgsApplication::processingRegistry() );
}

void TestQgsProcessing::addProvider()
{
  QgsProcessingRegistry r;
  QSignalSpy spyProviderAdded( &r, &QgsProcessingRegistry::providerAdded );

  QVERIFY( r.providers().isEmpty() );

  QVERIFY( !r.addProvider( nullptr ) );

  // add a provider
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( r.addProvider( p ) );
  QCOMPARE( r.providers(), QList< QgsProcessingProvider * >() << p );
  QCOMPARE( spyProviderAdded.count(), 1 );
  QCOMPARE( spyProviderAdded.last().at( 0 ).toString(), QString( "p1" ) );

  //try adding another provider
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );
  QCOMPARE( r.providers().toSet(), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );
  QCOMPARE( spyProviderAdded.last().at( 0 ).toString(), QString( "p2" ) );

  //try adding a provider with duplicate id
  DummyProvider *p3 = new DummyProvider( "p2" );
  QVERIFY( !r.addProvider( p3 ) );
  QCOMPARE( r.providers().toSet(), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );
  delete p3;

  // test that adding a provider which does not load means it is not added to registry
  DummyProviderNoLoad *p4 = new DummyProviderNoLoad( "p4" );
  QVERIFY( !r.addProvider( p4 ) );
  QCOMPARE( r.providers().toSet(), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );
  delete p4;
}

void TestQgsProcessing::providerById()
{
  QgsProcessingRegistry r;

  // no providers
  QVERIFY( !r.providerById( "p1" ) );

  // add a provider
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( r.addProvider( p ) );
  QCOMPARE( r.providerById( "p1" ), p );
  QVERIFY( !r.providerById( "p2" ) );

  //try adding another provider
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );
  QCOMPARE( r.providerById( "p1" ), p );
  QCOMPARE( r.providerById( "p2" ), p2 );
  QVERIFY( !r.providerById( "p3" ) );
}

void TestQgsProcessing::removeProvider()
{
  QgsProcessingRegistry r;
  QSignalSpy spyProviderRemoved( &r, &QgsProcessingRegistry::providerRemoved );

  QVERIFY( !r.removeProvider( nullptr ) );
  QVERIFY( !r.removeProvider( "p1" ) );
  // provider not in registry
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( !r.removeProvider( p ) );
  QCOMPARE( spyProviderRemoved.count(), 0 );

  // add some providers
  QVERIFY( r.addProvider( p ) );
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );

  // remove one by pointer
  bool unloaded = false;
  p->unloaded = &unloaded;
  QVERIFY( r.removeProvider( p ) );
  QCOMPARE( spyProviderRemoved.count(), 1 );
  QCOMPARE( spyProviderRemoved.last().at( 0 ).toString(), QString( "p1" ) );
  QCOMPARE( r.providers(), QList< QgsProcessingProvider * >() << p2 );

  //test that provider was unloaded
  QVERIFY( unloaded );

  // should fail, already removed
  QVERIFY( !r.removeProvider( "p1" ) );

  // remove one by id
  QVERIFY( r.removeProvider( "p2" ) );
  QCOMPARE( spyProviderRemoved.count(), 2 );
  QCOMPARE( spyProviderRemoved.last().at( 0 ).toString(), QString( "p2" ) );
  QVERIFY( r.providers().isEmpty() );
}

void TestQgsProcessing::compatibleLayers()
{
  QgsProject p;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QString raster3 = testDataDir + "/raster/band1_float32_noct_epsg4326.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );
  QFileInfo fi3( raster3 );
  QgsRasterLayer *r3 = new QgsRasterLayer( fi3.filePath(), "zz" );
  QVERIFY( r3->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  QgsVectorLayer *v3 = new QgsVectorLayer( "LineString", "v3", "memory" );
  QgsVectorLayer *v4 = new QgsVectorLayer( "none", "vvvv4", "memory" );

  p.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << r3 << v1 << v2 << v3 << v4 );

  // compatibleRasterLayers
  QVERIFY( QgsProcessingUtils::compatibleRasterLayers( nullptr ).isEmpty() );

  // sorted
  QStringList lIds;
  Q_FOREACH ( QgsRasterLayer *rl, QgsProcessingUtils::compatibleRasterLayers( &p ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "ar2" << "R1" << "zz" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsRasterLayer *rl, QgsProcessingUtils::compatibleRasterLayers( &p, false ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "R1" << "ar2" << "zz" );


  // compatibleVectorLayers
  QVERIFY( QgsProcessingUtils::compatibleVectorLayers( nullptr ).isEmpty() );

  // sorted
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" << "V4" << "vvvv4" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<QgsWkbTypes::GeometryType>(), false ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "V4" << "v1" << "v3" << "vvvv4" );

  // point only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<QgsWkbTypes::GeometryType>() << QgsWkbTypes::PointGeometry ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" );

  // polygon only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<QgsWkbTypes::GeometryType>() << QgsWkbTypes::PolygonGeometry ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "V4" );

  // line only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<QgsWkbTypes::GeometryType>() << QgsWkbTypes::LineGeometry ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v3" );

  // point and line only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<QgsWkbTypes::GeometryType>() << QgsWkbTypes::PointGeometry << QgsWkbTypes::LineGeometry ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" );


  // all layers
  QVERIFY( QgsProcessingUtils::compatibleLayers( nullptr ).isEmpty() );

  // sorted
  lIds.clear();
  Q_FOREACH ( QgsMapLayer *l, QgsProcessingUtils::compatibleLayers( &p ) )
    lIds << l->name();
  QCOMPARE( lIds, QStringList() << "ar2" << "R1" << "v1" << "v3" << "V4" << "vvvv4" <<  "zz" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsMapLayer *l, QgsProcessingUtils::compatibleLayers( &p, false ) )
    lIds << l->name();
  QCOMPARE( lIds, QStringList() << "R1" << "ar2" << "zz"  << "V4" << "v1" << "v3" << "vvvv4" );
}

void TestQgsProcessing::normalizeLayerSource()
{
  QCOMPARE( QgsProcessingUtils::normalizeLayerSource( "data\\layers\\test.shp" ), QString( "data/layers/test.shp" ) );
  QCOMPARE( QgsProcessingUtils::normalizeLayerSource( "data\\layers \"new\"\\test.shp" ), QString( "data/layers 'new'/test.shp" ) );
}

void TestQgsProcessing::context()
{
  QgsProcessingContext context;

  // simple tests for getters/setters
  context.setDefaultEncoding( "my_enc" );
  QCOMPARE( context.defaultEncoding(), QStringLiteral( "my_enc" ) );

  context.setFlags( QgsProcessingContext::Flags( 0 ) );
  QCOMPARE( context.flags(), QgsProcessingContext::Flags( 0 ) );

  QgsProject p;
  context.setProject( &p );
  QCOMPARE( context.project(), &p );

  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometrySkipInvalid );
  QCOMPARE( context.invalidGeometryCheck(), QgsFeatureRequest::GeometrySkipInvalid );

  // layers to load on completion
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V1", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Polygon", "V2", "memory" );
  QVERIFY( context.layersToLoadOnCompletion().isEmpty() );
  QMap< QString, QgsProcessingContext::LayerDetails > layers;
  layers.insert( v1->id(), QgsProcessingContext::LayerDetails( QStringLiteral( "v1" ), &p ) );
  context.setLayersToLoadOnCompletion( layers );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v1" ) );
  context.addLayerToLoadOnCompletion( v2->id(), QgsProcessingContext::LayerDetails( QStringLiteral( "v2" ), &p ) );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 2 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v1" ) );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 1 ), v2->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 1 ).name, QStringLiteral( "v2" ) );
  layers.clear();
  layers.insert( v2->id(), QgsProcessingContext::LayerDetails( QStringLiteral( "v2" ), &p ) );
  context.setLayersToLoadOnCompletion( layers );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v2->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v2" ) );
  context.addLayerToLoadOnCompletion( v1->id(), QgsProcessingContext::LayerDetails( QString(), &p ) );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 2 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 1 ), v2->id() );
  delete v1;
  delete v2;
}

void TestQgsProcessing::mapLayers()
{
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster = testDataDir + "landsat.tif";
  QString vector = testDataDir + "points.shp";

  // test loadMapLayerFromString with raster
  QgsMapLayer *l = QgsProcessingUtils::loadMapLayerFromString( raster );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayer::RasterLayer );
  delete l;

  //test with vector
  l = QgsProcessingUtils::loadMapLayerFromString( vector );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayer::VectorLayer );
  delete l;

  l = QgsProcessingUtils::loadMapLayerFromString( QString() );
  QVERIFY( !l );
  l = QgsProcessingUtils::loadMapLayerFromString( QStringLiteral( "so much room for activities!" ) );
  QVERIFY( !l );
  l = QgsProcessingUtils::loadMapLayerFromString( testDataDir + "multipoint.shp" );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayer::VectorLayer );
  delete l;
}

void TestQgsProcessing::mapLayerFromStore()
{
  // test mapLayerFromStore

  QgsMapLayerStore store;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  store.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << v1 << v2 );

  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QString(), nullptr ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QStringLiteral( "v1" ), nullptr ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QString(), &store ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( raster1, &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( raster2, &store ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "R1", &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "ar2", &store ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "V4", &store ), v1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "v1", &store ), v2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( r1->id(), &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( v1->id(), &store ), v1 );
}

void TestQgsProcessing::mapLayerFromString()
{
  // test mapLayerFromString

  QgsProcessingContext c;
  QgsProject p;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << v1 << v2 );

  // no project set yet
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QString(), c ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QStringLiteral( "v1" ), c ) );

  c.setProject( &p );

  // layers from current project
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QString(), c ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( raster1, c ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( raster2, c ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "R1", c ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "ar2", c ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V4", c ), v1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "v1", c ), v2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( r1->id(), c ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v1->id(), c ), v1 );

  // check that layers in context temporary store are used
  QgsVectorLayer *v5 = new QgsVectorLayer( "Polygon", "V5", "memory" );
  QgsVectorLayer *v6 = new QgsVectorLayer( "Point", "v6", "memory" );
  c.temporaryLayerStore()->addMapLayers( QList<QgsMapLayer *>() << v5 << v6 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V5", c ), v5 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "v6", c ), v6 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v5->id(), c ), v5 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v6->id(), c ), v6 );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( "aaaaa", c ) );

  // if specified, check that layers can be loaded
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( "aaaaa", c ) );
  QString newRaster = testDataDir + "requires_warped_vrt.tif";
  // don't allow loading
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( newRaster, c, false ) );
  // allow loading
  QgsMapLayer *loadedLayer = QgsProcessingUtils::mapLayerFromString( newRaster, c, true );
  QVERIFY( loadedLayer->isValid() );
  QCOMPARE( loadedLayer->type(), QgsMapLayer::RasterLayer );
  // should now be in temporary store
  QCOMPARE( c.temporaryLayerStore()->mapLayer( loadedLayer->id() ), loadedLayer );

  // since it's now in temporary store, should be accessible even if we deny loading new layers
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( newRaster, c, false ), loadedLayer );
}

void TestQgsProcessing::algorithm()
{
  DummyAlgorithm alg( "test" );
  DummyProvider *p = new DummyProvider( "p1" );
  QCOMPARE( alg.id(), QString( "test" ) );
  alg.setProvider( p );
  QCOMPARE( alg.provider(), p );
  QCOMPARE( alg.id(), QString( "p1:test" ) );

  QVERIFY( p->algorithms().isEmpty() );

  QSignalSpy providerRefreshed( p, &DummyProvider::algorithmsLoaded );
  p->refreshAlgorithms();
  QCOMPARE( providerRefreshed.count(), 1 );

  for ( int i = 0; i < 2; ++i )
  {
    QCOMPARE( p->algorithms().size(), 2 );
    QCOMPARE( p->algorithm( "alg1" )->name(), QStringLiteral( "alg1" ) );
    QCOMPARE( p->algorithm( "alg1" )->provider(), p );
    QCOMPARE( p->algorithm( "alg2" )->provider(), p );
    QCOMPARE( p->algorithm( "alg2" )->name(), QStringLiteral( "alg2" ) );
    QVERIFY( !p->algorithm( "aaaa" ) );
    QVERIFY( p->algorithms().contains( p->algorithm( "alg1" ) ) );
    QVERIFY( p->algorithms().contains( p->algorithm( "alg2" ) ) );

    // reload, then retest on next loop
    // must be safe for providers to reload their algorithms
    p->refreshAlgorithms();
    QCOMPARE( providerRefreshed.count(), 2 + i );
  }

  QgsProcessingRegistry r;
  r.addProvider( p );
  QCOMPARE( r.algorithms().size(), 2 );
  QVERIFY( r.algorithms().contains( p->algorithm( "alg1" ) ) );
  QVERIFY( r.algorithms().contains( p->algorithm( "alg2" ) ) );

  // algorithmById
  QCOMPARE( r.algorithmById( "p1:alg1" ), p->algorithm( "alg1" ) );
  QCOMPARE( r.algorithmById( "p1:alg2" ), p->algorithm( "alg2" ) );
  QVERIFY( !r.algorithmById( "p1:alg3" ) );
  QVERIFY( !r.algorithmById( "px:alg1" ) );

  //test that loading a provider triggers an algorithm refresh
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( p2->algorithms().isEmpty() );
  p2->load();
  QCOMPARE( p2->algorithms().size(), 2 );

  // test that adding a provider to the registry automatically refreshes algorithms (via load)
  DummyProvider *p3 = new DummyProvider( "p3" );
  QVERIFY( p3->algorithms().isEmpty() );
  r.addProvider( p3 );
  QCOMPARE( p3->algorithms().size(), 2 );
}

void TestQgsProcessing::features()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  for ( int i = 1; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setGeometry( QgsGeometry( new QgsPoint( 1, 2 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProject p;
  p.addMapLayer( layer );

  QgsProcessingContext context;
  context.setProject( &p );
  // disable check for geometry validity
  context.setFlags( QgsProcessingContext::Flags( 0 ) );

  std::function< QgsFeatureIds( QgsFeatureIterator it ) > getIds = []( QgsFeatureIterator it )
  {
    QgsFeature f;
    QgsFeatureIds ids;
    while ( it.nextFeature( f ) )
    {
      ids << f.id();
    }
    return ids;
  };

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );

  // test with all features
  QgsFeatureIds ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 2 << 3 << 4 << 5 );
  QCOMPARE( source->featureCount(), 5L );

  // test with selected features
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 2 << 4 );
  QCOMPARE( source->featureCount(), 2L );

  // selection, but not using selected features
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 2 << 3 << 4 << 5 );
  QCOMPARE( source->featureCount(), 5L );

  // using selected features, but no selection
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->removeSelection();
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( ids.isEmpty() );
  QCOMPARE( source->featureCount(), 0L );


  // test that feature request is honored
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures( QgsFeatureRequest().setFilterFids( QgsFeatureIds() << 1 << 3 << 5 ) ) );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 3 << 5 );

  // count is only rough - but we expect (for now) to see full layer count
  QCOMPARE( source->featureCount(), 5L );

  //test that feature request is honored when using selections
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures( QgsFeatureRequest().setFlags( QgsFeatureRequest::NoGeometry ) ) );
  QCOMPARE( ids, QgsFeatureIds() << 2 << 4 );

  // test callback is hit when filtering invalid geoms
  bool encountered = false;
  std::function< void( const QgsFeature & ) > callback = [ &encountered ]( const QgsFeature & )
  {
    encountered = true;
  };

  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryAbortOnInvalid );
  context.setInvalidGeometryCallback( callback );
  QgsVectorLayer *polyLayer = new QgsVectorLayer( "Polygon", "v2", "memory" );
  QgsFeature f;
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Polygon((0 0, 1 0, 0 1, 1 1, 0 0))" ) ) );
  polyLayer->dataProvider()->addFeatures( QgsFeatureList() << f );
  p.addMapLayer( polyLayer );
  params.insert( QStringLiteral( "layer" ), polyLayer->id() );

  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( encountered );

  encountered = false;
  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryNoCheck );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( !encountered );

}

void TestQgsProcessing::uniqueValues()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point?field=a:integer&field=b:string", "v1", "memory" );
  for ( int i = 0; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setAttributes( QgsAttributes() << i % 3 + 1 << QString( QChar( ( i % 3 ) + 65 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProcessingContext context;
  context.setFlags( QgsProcessingContext::Flags( 0 ) );

  QgsProject p;
  p.addMapLayer( layer );
  context.setProject( &p );

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );

  // some bad checks
  QVERIFY( source->uniqueValues( -1 ).isEmpty() );
  QVERIFY( source->uniqueValues( 10001 ).isEmpty() );

  // good checks
  QSet< QVariant > vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  QVERIFY( vals.contains( 3 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
  QVERIFY( vals.contains( QString( "C" ) ) );

  //using only selected features
  layer->selectByIds( QgsFeatureIds() << 1 << 2 << 4 );
  // but not using selection yet...
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  QVERIFY( vals.contains( 3 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
  QVERIFY( vals.contains( QString( "C" ) ) );

  // selection and using selection
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  QVERIFY( source->uniqueValues( -1 ).isEmpty() );
  QVERIFY( source->uniqueValues( 10001 ).isEmpty() );
  vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 2 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 2 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
}

void TestQgsProcessing::createIndex()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  for ( int i = 1; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setGeometry( QgsGeometry( new QgsPoint( i, 2 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProcessingContext context;
  QgsProject p;
  p.addMapLayer( layer );
  context.setProject( &p );

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  // disable selected features check
  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  QVERIFY( source.get() );
  QgsSpatialIndex index( *source.get() );
  QList<QgsFeatureId> ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 2 );

  // selected features check, but none selected
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  bool caught = false;
  try
  {
    index = QgsSpatialIndex( *source.get() );
    ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  }
  catch ( ... )
  {
    caught = true;
  }
  QVERIFY( caught );

  // create selection
  layer->selectByIds( QgsFeatureIds() << 4 << 5 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  index = QgsSpatialIndex( *source.get() );
  ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 4 );

  // selection but not using selection mode
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  index = QgsSpatialIndex( *source.get() );
  ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 2 );
}

void TestQgsProcessing::createFeatureSink()
{
  QgsProcessingContext context;

  // empty destination
  QString destination;
  destination = QString();
  QgsVectorLayer *layer = nullptr;

  // should create a memory layer
  std::unique_ptr< QgsFeatureSink > sink( QgsProcessingUtils::createFeatureSink( destination, context, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem() ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QgsFeature f;
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // specific memory layer output
  destination = QStringLiteral( "memory:mylayer" );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem() ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( layer->name(), QStringLiteral( "memory:mylayer" ) );
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // memory layer parameters
  destination = QStringLiteral( "memory:mylayer" );
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "my_field" ), QVariant::String, QString(), 100 ) );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::PointZM, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( layer->name(), QStringLiteral( "memory:mylayer" ) );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::PointZM );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 0 ).type(), QVariant::String );
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // non memory layer output
  destination = QDir::tempPath() + "/create_feature_sink.tab";
  QString prevDest = destination;
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Polygon, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  f = QgsFeature( fields );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Polygon((0 0, 0 1, 1 1, 1 0, 0 0 ))" ) ) );
  f.setAttributes( QgsAttributes() << "val" );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( destination, prevDest );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 0 ).type(), QVariant::String );
  QCOMPARE( layer->featureCount(), 1L );
  delete layer;
  layer = nullptr;

  // no extension, should default to shp
  destination = QDir::tempPath() + "/create_feature_sink2";
  prevDest = QDir::tempPath() + "/create_feature_sink2.shp";
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Point25D, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "PointZ(1 2 3)" ) ) );
  QVERIFY( sink->addFeature( f ) );
  QVERIFY( !layer );
  QCOMPARE( destination, prevDest );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::Point25D );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 0 ).type(), QVariant::String );
  QCOMPARE( layer->featureCount(), 1L );
  delete layer;
  layer = nullptr;

  //windows style path
  destination = "d:\\temp\\create_feature_sink.tab";
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Polygon, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
}

void TestQgsProcessing::parameters()
{
  // test parameter utilities

  std::unique_ptr< QgsProcessingParameterDefinition > def;
  QVariantMap params;
  params.insert( QStringLiteral( "prop" ), QgsProperty::fromField( "a_field" ) );
  params.insert( QStringLiteral( "string" ), QStringLiteral( "a string" ) );
  params.insert( QStringLiteral( "double" ), 5.2 );
  params.insert( QStringLiteral( "int" ), 15 );
  params.insert( QStringLiteral( "bool" ), true );

  QgsProcessingContext context;

  // isDynamic
  QVERIFY( QgsProcessingParameters::isDynamic( params, QStringLiteral( "prop" ) ) );
  QVERIFY( !QgsProcessingParameters::isDynamic( params, QStringLiteral( "string" ) ) );
  QVERIFY( !QgsProcessingParameters::isDynamic( params, QStringLiteral( "bad" ) ) );

  // parameterAsString
  def.reset( new QgsProcessingParameterString( QStringLiteral( "string" ), QStringLiteral( "desc" ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "a string" ) );
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ).left( 3 ), QStringLiteral( "5.2" ) );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "15" ) );
  def->setName( QStringLiteral( "bool" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "true" ) );
  def->setName( QStringLiteral( "bad" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  // string with dynamic property (feature not set)
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  // correctly setup feature
  QgsFields fields;
  fields.append( QgsField( "a_field", QVariant::String, QString(), 30 ) );
  QgsFeature f( fields );
  f.setAttribute( 0, QStringLiteral( "field value" ) );
  context.expressionContext().setFeature( f );
  context.expressionContext().setFields( fields );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "field value" ) );

  // as double
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 5.2 );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 15.0 );
  f.setAttribute( 0, QStringLiteral( "6.2" ) );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 6.2 );

  // as int
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 5 );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 15 );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 6 );

  // as bool
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  def->setName( QStringLiteral( "bool" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  f.setAttribute( 0, false );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );

  // as layer
  def->setName( QStringLiteral( "double" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  def->setName( QStringLiteral( "int" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(),  params, context ) );
  def->setName( QStringLiteral( "bool" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  def->setName( QStringLiteral( "prop" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );

  QVERIFY( context.temporaryLayerStore()->mapLayers().isEmpty() );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  f.setAttribute( 0, testDataDir + "/raster/band1_float32_noct_epsg4326.tif" );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  // make sure layer was loaded
  QVERIFY( !context.temporaryLayerStore()->mapLayers().isEmpty() );

  // parameters as sinks

  QgsWkbTypes::Type wkbType = QgsWkbTypes::PolygonM;
  QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:3111" ) );
  QString destId;
  def->setName( QStringLiteral( "string" ) );
  params.insert( QStringLiteral( "string" ), QStringLiteral( "memory:mem" ) );
  std::unique_ptr< QgsFeatureSink > sink;
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->fields().count(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "a_field" ) );
  QCOMPARE( layer->wkbType(), wkbType );
  QCOMPARE( layer->crs(), crs );

  // property defined sink destination
  params.insert( QStringLiteral( "prop" ), QgsProperty::fromExpression( "'memory:mem2'" ) );
  def->setName( QStringLiteral( "prop" ) );
  crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:3113" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->fields().count(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "a_field" ) );
  QCOMPARE( layer->wkbType(), wkbType );
  QCOMPARE( layer->crs(), crs );

  // QgsProcessingFeatureSinkDefinition as parameter
  QgsProject p;
  QgsProcessingOutputLayerDefinition fs( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  QVERIFY( context.layersToLoadOnCompletion().isEmpty() );
  params.insert( QStringLiteral( "fs" ), QVariant::fromValue( fs ) );
  def->setName( QStringLiteral( "fs" ) );
  crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:28356" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  QgsVectorFileWriter *writer = dynamic_cast< QgsVectorFileWriter *>( sink.get() );
  QVERIFY( writer );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), wkbType );
  QCOMPARE( layer->crs(), crs );

  // make sure layer was automatically added to list to load on completion
  QCOMPARE( context.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), destId );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "desc" ) );

  // with name overloading
  QgsProcessingContext context2;
  fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  fs.destinationName = QStringLiteral( "my_dest" );
  params.insert( QStringLiteral( "fs" ), QVariant::fromValue( fs ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context2, destId ) );
  QVERIFY( sink.get() );
  QCOMPARE( context2.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), destId );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "my_dest" ) );
}

void TestQgsProcessing::algorithmParameters()
{
  DummyAlgorithm alg( "test" );
  alg.runParameterChecks();
}

void TestQgsProcessing::algorithmOutputs()
{
  DummyAlgorithm alg( "test" );
  alg.runOutputChecks();
}

void TestQgsProcessing::parameterGeneral()
{
  // test constructor
  QgsProcessingParameterBoolean param( "p1", "desc", true, true );
  QCOMPARE( param.name(), QString( "p1" ) );
  QCOMPARE( param.description(), QString( "desc" ) );
  QCOMPARE( param.defaultValue(), QVariant( true ) );
  QVERIFY( param.flags() & QgsProcessingParameterDefinition::FlagOptional );
  QVERIFY( param.dependsOnOtherParameters().isEmpty() );

  // test getters and setters
  param.setDescription( "p2" );
  QCOMPARE( param.description(), QString( "p2" ) );
  param.setDefaultValue( false );
  QCOMPARE( param.defaultValue(), QVariant( false ) );
  param.setFlags( QgsProcessingParameterDefinition::FlagHidden );
  QCOMPARE( param.flags(), QgsProcessingParameterDefinition::FlagHidden );
  param.setDefaultValue( true );
  QCOMPARE( param.defaultValue(), QVariant( true ) );
  param.setDefaultValue( QVariant() );
  QCOMPARE( param.defaultValue(), QVariant() );

  QVariantMap metadata;
  metadata.insert( "p1", 5 );
  metadata.insert( "p2", 7 );
  param.setMetadata( metadata );
  QCOMPARE( param.metadata(), metadata );
  param.metadata().insert( "p3", 9 );
  QCOMPARE( param.metadata().value( "p3" ).toInt(), 9 );

  QVariantMap map = param.toVariantMap();
  QgsProcessingParameterBoolean fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), param.name() );
  QCOMPARE( fromMap.description(), param.description() );
  QCOMPARE( fromMap.flags(), param.flags() );
  QCOMPARE( fromMap.defaultValue(), param.defaultValue() );
  QCOMPARE( fromMap.metadata(), param.metadata() );
}

void TestQgsProcessing::parameterBoolean()
{
  QgsProcessingContext context;

  // test no def
  QVariantMap params;
  params.insert( "no_def",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  params.insert( "no_def",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  params.insert( "no_def",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  params.remove( "no_def" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );

  // with defs

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterBoolean( "non_optional_default_false" ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "non_optional_default_false",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.insert( "non_optional_default_false",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "non_optional_default_false",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "non_optional_default_false",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );

  //non-optional - behavior is undefined, but internally default to false
  params.insert( "non_optional_default_false",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.remove( "non_optional_default_false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );

  QCOMPARE( def->valueAsPythonString( false, context ), QStringLiteral( "False" ) );
  QCOMPARE( def->valueAsPythonString( true, context ), QStringLiteral( "True" ) );
  QCOMPARE( def->valueAsPythonString( "false", context ), QStringLiteral( "False" ) );
  QCOMPARE( def->valueAsPythonString( "true", context ), QStringLiteral( "True" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional_default_false=boolean false" ) );
  std::unique_ptr< QgsProcessingParameterBoolean > fromCode( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional default false" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), false );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterBoolean fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( QgsProcessingParameters::parameterFromVariantMap( map ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterBoolean *>( def.get() ) );


  def.reset( new QgsProcessingParameterBoolean( "optional_default_true", QString(), true, true ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional_default_true",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.insert( "optional_default_true",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "optional_default_true",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "optional_default_true",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  //optional - should be default
  params.insert( "optional_default_true",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.remove( "optional_default_true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional_default_true=optional boolean true" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional default true" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), true );

  def.reset( new QgsProcessingParameterBoolean( "optional_default_false", QString(), false, true ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional_default_false",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.insert( "optional_default_false",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "optional_default_false",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "optional_default_false",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  //optional - should be default
  params.insert( "optional_default_false",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.remove( "optional_default_false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional default false" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), false );

  def.reset( new QgsProcessingParameterBoolean( "non_optional_default_true", QString(), true, false ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "non_optional_default_true",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  params.insert( "non_optional_default_true",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "non_optional_default_true",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.insert( "non_optional_default_true",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  //non-optional - behavior is undefined, but internally fallback to default
  params.insert( "non_optional_default_true",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  params.remove( "non_optional_default_true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional default true" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), true );
}

void TestQgsProcessing::parameterCrs()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterCrs > def( new QgsProcessingParameterCrs( "non_optional", QString(), QString( "EPSG:3113" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:12003" ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:3111" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // using map layer
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );
  QVERIFY( def->checkValueIsAcceptable( v1->id() ) );

  // special ProjectCrs string
  params.insert( "non_optional",  QStringLiteral( "ProjectCrs" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:28353" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "ProjectCrs" ) ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:4326" ) );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );

  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:32633" ) );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );

  // string representation of a crs
  params.insert( "non_optional", QString( "EPSG:28355" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:28355" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "EPSG:28355" ) ) );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).isValid() );

  QCOMPARE( def->valueAsPythonString( "EPSG:12003", context ), QStringLiteral( "'EPSG:12003'" ) );
  QCOMPARE( def->valueAsPythonString( "ProjectCrs", context ), QStringLiteral( "'ProjectCrs'" ) );
  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterCrs fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterCrs *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterCrs *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=crs EPSG:3113" ) );
  std::unique_ptr< QgsProcessingParameterCrs > fromCode( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterCrs( "optional", QString(), QString( "EPSG:3113" ), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3113" ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:12003" ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:3111" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional crs EPSG:3113" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  code = QStringLiteral( "##optional=optional crs None" );
  fromCode.reset( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
}

void TestQgsProcessing::parameterLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterMapLayer > def( new QgsProcessingParameterMapLayer( "non_optional", QString(), QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( v1->id() ) );
  QVERIFY( def->checkValueIsAcceptable( v1->id(), &context ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), r1->id() );
  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->publicSource(), raster2 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );

  // layer
  params.insert( "non_optional", QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ), r1 );
  params.insert( "non_optional", QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ), v1 );

  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer" ) );
  std::unique_ptr< QgsProcessingParameterMapLayer > fromCode( dynamic_cast< QgsProcessingParameterMapLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMapLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterMapLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMapLayer *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterMapLayer( "optional", QString(), v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional layer " ) + v1->id() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMapLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterExtent()
{
  // setup a context
  QgsProject p;
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  p.addMapLayers( QList<QgsMapLayer *>() << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterExtent > def( new QgsProcessingParameterExtent( "non_optional", QString(), QString( "1,2,3,4" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,4" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // these checks require a context - otherwise we could potentially be referring to a layer source
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2,3", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2,3,a", &context ) );

  // using map layer
  QVariantMap params;
  params.insert( "non_optional",  r1->id() );
  QVERIFY( def->checkValueIsAcceptable( r1->id() ) );
  QgsRectangle ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QCOMPARE( ext, r1->extent() );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtent( def.get(), params, context ),  r1->extent() );

  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 781662.375000, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 793062.375000, 10 );
  QGSCOMPARENEAR( ext.yMinimum(),  3339523.125000, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 3350923.125000, 10 );

  // string representation of an extent
  params.insert( "non_optional", QString( "1.1,2.2,3.3,4.4" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "1.1,2.2,3.3,4.4" ) ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 1.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 2.2, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  3.3, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 4.4, 0.001 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  QVERIFY( QgsProcessingParameters::parameterAsExtent( def.get(), params, context ).isNull() );

  QCOMPARE( def->valueAsPythonString( "1,2,3,4", context ), QStringLiteral( "'1,2,3,4'" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( raster2, context ), QString( "'" ) + testDataDir + QStringLiteral( "landsat.tif'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=extent 1,2,3,4" ) );
  std::unique_ptr< QgsProcessingParameterExtent > fromCode( dynamic_cast< QgsProcessingParameterExtent * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterExtent fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterExtent *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterExtent *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterExtent( "optional", QString(), QString( "5,6,7,8" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,4" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  // Extent is unique in that it will let you set invalid, whereas other
  // optional parameters become "default" when assigning invalid.
  params.insert( "optional",  QVariant() );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QVERIFY( ext.isNull() );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional extent 5,6,7,8" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterExtent * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterPoint()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterPoint > def( new QgsProcessingParameterPoint( "non_optional", QString(), QString( "1,2" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a point
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1,2.2" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2.2" ) );
  QgsPointXY point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QCOMPARE( point.x(), 0.0 );
  QCOMPARE( point.y(), 0.0 );

  QCOMPARE( def->valueAsPythonString( "1,2", context ), QStringLiteral( "'1,2'" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=point 1,2" ) );
  std::unique_ptr< QgsProcessingParameterPoint > fromCode( dynamic_cast< QgsProcessingParameterPoint * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterPoint fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterPoint *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterPoint *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterPoint( "optional", QString(), QString( "5.1,6.2" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 5.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 6.2, 0.001 );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional point 5.1,6.2" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterPoint * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterFile()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterFile > def( new QgsProcessingParameterFile( "non_optional", QString(), QgsProcessingParameterFile::File, QString(), QString( "abc.bmp" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( "  " ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a file
  QVariantMap params;
  params.insert( "non_optional", QString( "def.bmp" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFile( def.get(), params, context ), QString( "def.bmp" ) );

  // with extension
  def.reset( new QgsProcessingParameterFile( "non_optional", QString(), QgsProcessingParameterFile::File, QStringLiteral( ".bmp" ), QString( "abc.bmp" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.BMP" ) );
  QVERIFY( !def->checkValueIsAcceptable( "bricks.pcx" ) );

  QCOMPARE( def->valueAsPythonString( "bricks.bmp", context ), QStringLiteral( "'bricks.bmp'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=file abc.bmp" ) );
  std::unique_ptr< QgsProcessingParameterFile > fromCode( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFile fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.extension(), def->extension() );
  QCOMPARE( fromMap.behavior(), def->behavior() );
  def.reset( dynamic_cast< QgsProcessingParameterFile *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFile *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterFile( "optional", QString(), QgsProcessingParameterFile::File, QString(), QString( "gef.bmp" ),  true ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( "  " ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFile( def.get(), params, context ), QString( "gef.bmp" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional file gef.bmp" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );

  // folder
  def.reset( new QgsProcessingParameterFile( "optional", QString(), QgsProcessingParameterFile::Folder, QString(), QString( "/home/me" ),  true ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional folder /home/me" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );
}

void TestQgsProcessing::parameterMatrix()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterMatrix > def( new QgsProcessingParameterMatrix( "non_optional", QString(), 3, false, QStringList(), QString( ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 << 2 << 3 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << ( QVariantList() << 1 << 2 << 3 ) << ( QVariantList() << 1 << 2 << 3 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // list
  QVariantMap params;
  params.insert( "non_optional", QVariantList() << 1 << 2 << 3 );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 1 << 2 << 3 );

  //string
  params.insert( "non_optional", QString( "4,5,6" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 4 << 5 << 6 );

  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "[5]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1 << 2 << 3, context ), QStringLiteral( "[1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << ( QVariantList() << 1 << 2 << 3 ) << ( QVariantList() << 1 << 2 << 3 ), context ), QStringLiteral( "[1,2,3,1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( "1,2,3", context ), QStringLiteral( "[1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=matrix" ) );
  std::unique_ptr< QgsProcessingParameterMatrix > fromCode( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMatrix fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.headers(), def->headers() );
  QCOMPARE( fromMap.numberRows(), def->numberRows() );
  QCOMPARE( fromMap.hasFixedNumberRows(), def->hasFixedNumberRows() );
  def.reset( dynamic_cast< QgsProcessingParameterMatrix *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMatrix *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterMatrix( "optional", QString(), 3, false, QStringList(), QVariantList() << 4 << 5 << 6,  true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 << 2 << 3 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional matrix" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 4 << 5 << 6 );
  def.reset( new QgsProcessingParameterMatrix( "optional", QString(), 3, false, QStringList(), QString( "1,2,3" ),  true ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional matrix 1,2,3" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 1 << 2 << 3 );
}

void TestQgsProcessing::parameterLayerList()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterMultipleLayers > def( new QgsProcessingParameterMultipleLayers( "non_optional", QString(), QgsProcessingParameterDefinition::TypeAny, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );

  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );


  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );
  // using existing map layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  // using two existing map layer ID
  params.insert( "non_optional",  QVariantList() << v1->id() << r1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  // using two existing map layers
  params.insert( "non_optional",  QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  // mix of existing layers and non project layer string
  params.insert( "non_optional",  QVariantList() << v1->id() << raster2 );
  QList< QgsMapLayer *> layers = QgsProcessingParameters::parameterAsLayerList( def.get(), params, context );
  QCOMPARE( layers.at( 0 ), v1 );
  QCOMPARE( layers.at( 1 )->publicSource(), raster2 );

  // empty string
  params.insert( "non_optional",  QString( "" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ).isEmpty() );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ).isEmpty() );

  // with 2 min inputs
  def->setMinimumNumberInputs( 2 );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  def->setMinimumNumberInputs( 3 );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );

  QCOMPARE( def->valueAsPythonString( "layer12312312", context ), QStringLiteral( "'layer12312312'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc']" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc']" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << r1->id() << raster2, context ), QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc','" ) + testDataDir + QStringLiteral( "landsat.tif']" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=multiple vector" ) );
  std::unique_ptr< QgsProcessingParameterMultipleLayers > fromCode( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), QgsProcessingParameterDefinition::TypeVectorAny );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMultipleLayers fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.layerType(), def->layerType() );
  QCOMPARE( fromMap.minimumNumberInputs(), def->minimumNumberInputs() );
  def.reset( dynamic_cast< QgsProcessingParameterMultipleLayers *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMultipleLayers *>( def.get() ) );

  // optional with one default layer
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessingParameterDefinition::TypeAny, v1->id(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );

  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional multiple vector " ) + v1->id() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->layerType(), QgsProcessingParameterDefinition::TypeVectorAny );

  // optional with two default layers
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessingParameterDefinition::TypeAny, QVariantList() << v1->id() << r1->publicSource(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional multiple vector " ) + v1->id() + "," + r1->publicSource() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), v1->id() + "," + r1->publicSource() );
  QCOMPARE( fromCode->layerType(), QgsProcessingParameterDefinition::TypeVectorAny );

  // optional with one default direct layer
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessingParameterDefinition::TypeAny, QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  // optional with two default direct layers
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessingParameterDefinition::TypeAny, QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( r1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  def.reset( new QgsProcessingParameterMultipleLayers( "type", QString(), QgsProcessingParameterDefinition::TypeRaster ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##type=multiple raster" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "type" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), QgsProcessingParameterDefinition::TypeRaster );

  def.reset( new QgsProcessingParameterMultipleLayers( "type", QString(), QgsProcessingParameterDefinition::TypeFile ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##type=multiple file" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "type" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), def->layerType() );
}

void TestQgsProcessing::parameterNumber()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterNumber > def( new QgsProcessingParameterNumber( "non_optional", QString(), QgsProcessingParameterNumber::Double, 5, false ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1" ) );
  double number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  int iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // double
  params.insert( "non_optional", 1.1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // int
  params.insert( "non_optional", 1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QCOMPARE( number, 5.0 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );

  // with min value
  def->setMinimum( 11 );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 25 ) );
  QVERIFY( def->checkValueIsAcceptable( "21.1" ) );
  // with max value
  def->setMaximum( 21 );
  QVERIFY( !def->checkValueIsAcceptable( 35 ) );
  QVERIFY( !def->checkValueIsAcceptable( "31.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 15 ) );
  QVERIFY( def->checkValueIsAcceptable( "11.1" ) );

  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1.1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=number 5" ) );
  std::unique_ptr< QgsProcessingParameterNumber > fromCode( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );


  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterNumber fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterNumber *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterNumber *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterNumber( "optional", QString(), QgsProcessingParameterNumber::Double, 5.4, true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );

  code = def->asScriptCode();
  QCOMPARE( code.left( 30 ), QStringLiteral( "##optional=optional number 5.4" ) ); // truncate code to 30, to avoid Qt 5.6 rounding issues
  fromCode.reset( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional number None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
}

void TestQgsProcessing::parameterRange()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterRange > def( new QgsProcessingParameterRange( "non_optional", QString(), QgsProcessingParameterNumber::Double, QString( "5,6" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2,3" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,a" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 << 3 ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a range of numbers
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1,1.2" ) );
  QList< double > range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // list
  params.insert( "non_optional", QVariantList() << 1.1 << 1.2 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // too many elements:
  params.insert( "non_optional", QString( "1.1,1.2,1.3" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );
  params.insert( "non_optional", QVariantList() << 1.1 << 1.2 << 1.3 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params,  context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // not enough elements - don't care about the result, just don't crash!
  params.insert( "non_optional", QString( "1.1" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  params.insert( "non_optional", QVariantList() << 1.1 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );

  QCOMPARE( def->valueAsPythonString( "1.1,2", context ), QStringLiteral( "[1.1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1.1 << 2, context ), QStringLiteral( "[1.1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=range 5,6" ) );
  std::unique_ptr< QgsProcessingParameterRange > fromCode( dynamic_cast< QgsProcessingParameterRange * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );


  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRange fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterRange *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRange *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterRange( "optional", QString(), QgsProcessingParameterNumber::Double, QString( "5.4,7.4" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 5.4, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 7.4, 0.001 );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional range 5.4,7.4" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRange * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterRasterLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterRasterLayer > def( new QgsProcessingParameterRasterLayer( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  r1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  // using existing map layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  // not raster layer
  params.insert( "non_optional",  v1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  // using existing vector layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );
  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->publicSource(), raster2 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=raster" ) );
  std::unique_ptr< QgsProcessingParameterRasterLayer > fromCode( dynamic_cast< QgsProcessingParameterRasterLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterRasterLayer( "optional", QString(), r1->id(), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional raster " ) + r1->id() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRasterLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRasterLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterRasterLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRasterLayer *>( def.get() ) );

  // optional with direct layer
  def.reset( new QgsProcessingParameterRasterLayer( "optional", QString(), QVariant::fromValue( r1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );
}

void TestQgsProcessing::parameterEnum()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterEnum > def( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", false, 2, false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1" ) );
  int iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // double
  params.insert( "non_optional", 2.2 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  // int
  params.insert( "non_optional", 1 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  // out of range
  params.insert( "non_optional", 4 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum A;B;C 2" ) );
  std::unique_ptr< QgsProcessingParameterEnum > fromCode( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterEnum fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.options(), def->options() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  def.reset( dynamic_cast< QgsProcessingParameterEnum *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterEnum *>( def.get() ) );

  // multiple
  def.reset( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", true, 5, false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "non_optional", QString( "1,2" ) );
  QList< int > iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  params.insert( "non_optional", QVariantList() << 0 << 2 );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 0 << 2 );

  // empty list
  params.insert( "non_optional", QVariantList() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() );

  QCOMPARE( def->valueAsPythonString( QVariantList() << 1 << 2, context ), QStringLiteral( "[1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1,2" ), context ), QStringLiteral( "[1,2]" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum multiple A;B;C 5" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  // optional
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "a" << "b", false, 5, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional enum a;b 5" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  params.insert( "optional",  QVariant() );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  //optional with multiples
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "A" << "B" << "C", true, QVariantList() << 1 << 2, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "A" << "B" << "C", true, "1,2", true ) );
  params.insert( "optional",  QVariant() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  // empty list
  params.insert( "optional", QVariantList() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional enum multiple A;B;C 1,2" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
}

void TestQgsProcessing::parameterString()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterString > def( new QgsProcessingParameterString( "non_optional", QString(), QString(), false, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=string" ) );
  std::unique_ptr< QgsProcessingParameterString > fromCode( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterString fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.multiLine(), def->multiLine() );
  def.reset( dynamic_cast< QgsProcessingParameterString *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterString *>( def.get() ) );

  def->setMultiLine( true );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=string long" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );
  def->setMultiLine( false );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  // optional
  def.reset( new QgsProcessingParameterString( "optional", QString(), QString( "default" ), false, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional string default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  def->setMultiLine( true );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional string long default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );
}

void TestQgsProcessing::parameterExpression()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterExpression > def( new QgsProcessingParameterExpression( "non_optional", QString(), QString(), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=expression" ) );
  std::unique_ptr< QgsProcessingParameterExpression > fromCode( dynamic_cast< QgsProcessingParameterExpression * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterExpression fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameter(), def->parentLayerParameter() );
  def.reset( dynamic_cast< QgsProcessingParameterExpression *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterExpression *>( def.get() ) );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameter( QStringLiteral( "test_layer" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "test_layer" ) );

  // optional
  def.reset( new QgsProcessingParameterExpression( "optional", QString(), QString( "default" ), QString(), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "default" ) );
  // valid expression, should not fallback
  params.insert( "optional",  QVariant( "1+2" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "1+2" ) );
  // invalid expression, should fallback
  params.insert( "optional",  QVariant( "1+" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "default" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional expression default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterExpression * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterField()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterField > def( new QgsProcessingParameterField( "non_optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, false, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "a" ) );
  QStringList fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=field" ) );
  std::unique_ptr< QgsProcessingParameterField > fromCode( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameter( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  def->setDataType( QgsProcessingParameterField::Numeric );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  def->setDataType( QgsProcessingParameterField::String );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  def->setDataType( QgsProcessingParameterField::DateTime );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  // multiple
  def.reset( new QgsProcessingParameterField( "non_optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, true, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "non_optional", QString( "a;b" ) );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" << "b" );
  params.insert( "non_optional", QVariantList() << "a" << "b" );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" << "b" );

  QCOMPARE( def->valueAsPythonString( QStringList() << "a" << "b", context ), QStringLiteral( "['a','b']" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << "a" << "b", context ), QStringLiteral( "['a','b']" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterField fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  def.reset( dynamic_cast< QgsProcessingParameterField *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterField *>( def.get() ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  // optional
  def.reset( new QgsProcessingParameterField( "optional", QString(), QString( "def" ), QString(), QgsProcessingParameterField::Any, false, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "def" );

  // optional, no default
  def.reset( new QgsProcessingParameterField( "optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, false, true ) );
  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QVERIFY( fields.isEmpty() );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional field" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameter(), def->parentLayerParameter() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  //optional with multiples
  def.reset( new QgsProcessingParameterField( "optional", QString(), QString( "abc;def" ), QString(), QgsProcessingParameterField::Any, true, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "abc" << "def" );
  def.reset( new QgsProcessingParameterField( "optional", QString(), QVariantList() << "abc" << "def", QString(), QgsProcessingParameterField::Any, true, true ) );
  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "abc" << "def" );
}

void TestQgsProcessing::parameterVectorLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector1 = testDataDir + "multipoint.shp";
  QString raster = testDataDir + "landsat.tif";
  QFileInfo fi1( raster );
  QFileInfo fi2( vector1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( fi2.filePath(), "V4", "ogr" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterVectorLayer > def( new QgsProcessingParameterVectorLayer( "non_optional", QString(), QString( "somelayer" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // using existing layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // not vector layer
  params.insert( "non_optional",  r1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // using existing non-vector layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // string representing a layer source
  params.insert( "non_optional", vector1 );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->publicSource(), vector1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( vector1, context ), QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) );
  QCOMPARE( def->valueAsPythonString( v1->id(), context ), QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( v1 ), context ), QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vector somelayer" ) );
  std::unique_ptr< QgsProcessingParameterVectorLayer > fromCode( dynamic_cast< QgsProcessingParameterVectorLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterVectorLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterVectorLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterVectorLayer *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterVectorLayer( "optional", QString(), v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional vector " ) + v1->id() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  //optional with direct layer default
  def.reset( new QgsProcessingParameterVectorLayer( "optional", QString(), QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
}

void TestQgsProcessing::parameterFeatureSource()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector1 = testDataDir + "multipoint.shp";
  QString raster = testDataDir + "landsat.tif";
  QFileInfo fi1( raster );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFeatureSource > def( new QgsProcessingParameterFeatureSource( "non_optional", QString(), QList< int >() << QgsProcessingParameterDefinition::TypeVectorAny, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // using existing layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // not vector layer
  params.insert( "non_optional",  r1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // using existing non-vector layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // string representing a layer source
  params.insert( "non_optional", vector1 );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->publicSource(), vector1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( "abc" ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', False)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), true ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', True)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), False)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFeatureSource fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataTypes(), def->dataTypes() );
  def.reset( dynamic_cast< QgsProcessingParameterFeatureSource *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFeatureSource *>( def.get() ) );


  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source" ) );
  std::unique_ptr< QgsProcessingParameterFeatureSource > fromCode( dynamic_cast< QgsProcessingParameterFeatureSource * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  def->setDataTypes( QList< int >() << QgsProcessingParameterDefinition::TypeVectorPoint );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point" ) );
  def->setDataTypes( QList< int >() << QgsProcessingParameterDefinition::TypeVectorLine );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source line" ) );
  def->setDataTypes( QList< int >() << QgsProcessingParameterDefinition::TypeVectorPolygon );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source polygon" ) );
  def->setDataTypes( QList< int >() << QgsProcessingParameterDefinition::TypeVectorPoint << QgsProcessingParameterDefinition::TypeVectorLine );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point line" ) );
  def->setDataTypes( QList< int >() << QgsProcessingParameterDefinition::TypeVectorPoint << QgsProcessingParameterDefinition::TypeVectorPolygon );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point polygon" ) );


  // optional
  def.reset( new QgsProcessingParameterFeatureSource( "optional", QString(), QList< int >() << QgsProcessingParameterDefinition::TypeVectorAny, v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional source " ) + v1->id() );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSource * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );


  //optional with direct layer default
  def.reset( new QgsProcessingParameterFeatureSource( "optional", QString(), QList< int >() << QgsProcessingParameterDefinition::TypeVectorAny, QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
}

void TestQgsProcessing::parameterFeatureSink()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFeatureSink > def( new QgsProcessingParameterFeatureSink( "non_optional", QString(), QgsProcessingParameterDefinition::TypeVectorAny, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'))" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "shp" ) );
  QCOMPARE( def->generateTemporaryDestination(), QStringLiteral( "memory:" ) );
  def->setSupportsNonFileBasedOutputs( false );
  QVERIFY( def->generateTemporaryDestination().endsWith( QStringLiteral( ".shp" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFeatureSink fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.supportsNonFileBasedOutputs(), def->supportsNonFileBasedOutputs() );
  def.reset( dynamic_cast< QgsProcessingParameterFeatureSink *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFeatureSink *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink" ) );
  std::unique_ptr< QgsProcessingParameterFeatureSink > fromCode( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  def->setDataType( QgsProcessingParameterDefinition::TypeVectorPoint );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink point" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessingParameterDefinition::TypeVectorLine );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink line" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessingParameterDefinition::TypeVectorPolygon );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink polygon" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessingParameterDefinition::TypeTable );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink table" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // optional
  def.reset( new QgsProcessingParameterFeatureSink( "optional", QString(), QgsProcessingParameterDefinition::TypeVectorAny, QString(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional sink" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // test hasGeometry
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeAny ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeVectorAny ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeVectorPoint ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeVectorLine ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeVectorPolygon ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeRaster ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeFile ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessingParameterDefinition::TypeTable ).hasGeometry() );

}

void TestQgsProcessing::parameterVectorOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterVectorOutput > def( new QgsProcessingParameterVectorOutput( "non_optional", QString(), QgsProcessingParameterDefinition::TypeVectorAny, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'))" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "shp" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QStringLiteral( ".shp" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterVectorOutput fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterVectorOutput *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterVectorOutput *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorOut" ) );
  std::unique_ptr< QgsProcessingParameterVectorOutput > fromCode( dynamic_cast< QgsProcessingParameterVectorOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  def->setDataType( QgsProcessingParameterDefinition::TypeVectorPoint );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorOut point" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessingParameterDefinition::TypeVectorLine );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorOut line" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessingParameterDefinition::TypeVectorPolygon );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorOut polygon" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // optional
  def.reset( new QgsProcessingParameterVectorOutput( "optional", QString(), QgsProcessingParameterDefinition::TypeVectorAny, QString(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional vectorOut" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // test hasGeometry
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeAny ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeVectorAny ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeVectorPoint ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeVectorLine ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeVectorPolygon ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeRaster ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeFile ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorOutput( "test", QString(), QgsProcessingParameterDefinition::TypeTable ).hasGeometry() );

}

void TestQgsProcessing::parameterRasterOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterRasterOutput > def( new QgsProcessingParameterRasterOutput( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif", &context ) );

  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "tif" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QStringLiteral( ".tif" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVariantMap params;
  params.insert( "non_optional", "test.tif" );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );
  params.insert( "non_optional", QgsProcessingOutputLayerDefinition( "test.tif" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'))" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRasterOutput fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.supportsNonFileBasedOutputs(), def->supportsNonFileBasedOutputs() );
  def.reset( dynamic_cast< QgsProcessingParameterRasterOutput *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRasterOutput *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=rasterOut" ) );
  std::unique_ptr< QgsProcessingParameterRasterOutput > fromCode( dynamic_cast< QgsProcessingParameterRasterOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterRasterOutput( "optional", QString(), QString( "default.tif" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterOutputLayer( def.get(), params, context ), QStringLiteral( "default.tif" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional rasterOut default.tif" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRasterOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // test layers to load on completion
  def.reset( new QgsProcessingParameterRasterOutput( "x", QStringLiteral( "desc" ), QStringLiteral( "default.tif" ), true ) );
  QgsProcessingOutputLayerDefinition fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.tif" ) );
  fs.destinationProject = &p;
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );

  // make sure layer was automatically added to list to load on completion
  QCOMPARE( context.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "desc" ) );

  // with name overloading
  QgsProcessingContext context2;
  fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.tif" ) );
  fs.destinationProject = &p;
  fs.destinationName = QStringLiteral( "my_dest" );
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterOutputLayer( def.get(), params, context2 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "my_dest" ) );
}

void TestQgsProcessing::parameterFileOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFileOutput > def( new QgsProcessingParameterFileOutput( "non_optional", QString(), QStringLiteral( "BMP files (*.bmp)" ), QVariant(), false ) );
  QCOMPARE( def->fileFilter(), QStringLiteral( "BMP files (*.bmp)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "bmp" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QStringLiteral( ".bmp" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx)" ) );
  QCOMPARE( def->fileFilter(), QStringLiteral( "PCX files (*.pcx)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx *.picx)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx *.picx);;BMP files (*.bmp)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  def->setFileFilter( QString() );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "file" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QStringLiteral( ".file" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  // should be OK with or without context - it's an output file!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt", &context ) );

  QVariantMap params;
  params.insert( "non_optional", "test.txt" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "test.txt" ) );
  params.insert( "non_optional", QgsProcessingOutputLayerDefinition( "test.txt" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "test.txt" ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition('abc')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProcessingOutputLayerDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'))" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFileOutput fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.fileFilter(), def->fileFilter() );
  QCOMPARE( fromMap.supportsNonFileBasedOutputs(), def->supportsNonFileBasedOutputs() );
  def.reset( dynamic_cast< QgsProcessingParameterFileOutput *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFileOutput *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=fileOut" ) );
  std::unique_ptr< QgsProcessingParameterFileOutput > fromCode( dynamic_cast< QgsProcessingParameterFileOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterFileOutput( "optional", QString(), QString(), QString( "default.txt" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "default.txt" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional fileOut default.txt" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFileOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterFolderOut()
{
  // setup a context
  QgsProject p;
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFolderOutput > def( new QgsProcessingParameterFolderOutput( "non_optional", QString(), QVariant(), false ) );

  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "asdasd" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // should be OK with or without context - it's an output folder!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/", &context ) );

  QVariantMap params;
  params.insert( "non_optional", "c:/mine" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "c:/mine" ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFolderOutput fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.supportsNonFileBasedOutputs(), def->supportsNonFileBasedOutputs() );
  def.reset( dynamic_cast< QgsProcessingParameterFolderOutput *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFolderOutput *>( def.get() ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=folderOut" ) );
  std::unique_ptr< QgsProcessingParameterFolderOutput > fromCode( dynamic_cast< QgsProcessingParameterFolderOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterFolderOutput( "optional", QString(), QString( "c:/junk" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "c:/junk" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional folderOut c:/junk" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFolderOutput * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::checkParamValues()
{
  DummyAlgorithm a( "asd" );
  a.checkParameterVals();
}

void TestQgsProcessing::combineLayerExtent()
{
  QgsRectangle ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() );
  QVERIFY( ext.isNull() );

  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt

  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "R2" );

  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1 );
  QGSCOMPARENEAR( ext.xMinimum(), 1535375.000000, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 1535475, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 5083255, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 5083355, 10 );

  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1 << r2 );
  QGSCOMPARENEAR( ext.xMinimum(), 781662, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 1535475, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 3339523, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 5083355, 10 );

  // with reprojection
  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1 << r2, QgsCoordinateReferenceSystem::fromEpsgId( 3785 ) );
  QGSCOMPARENEAR( ext.xMinimum(), 1995320, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 2008833, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 3523084, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 3536664, 10 );
}

void TestQgsProcessing::processingFeatureSource()
{
  QString sourceString = QStringLiteral( "test.shp" );
  QgsProcessingFeatureSourceDefinition fs( sourceString, true );
  QCOMPARE( fs.source.staticValue().toString(), sourceString );
  QVERIFY( fs.selectedFeaturesOnly );

  // test storing QgsProcessingFeatureSource in variant and retrieving
  QVariant fsInVariant = QVariant::fromValue( fs );
  QVERIFY( fsInVariant.isValid() );

  QgsProcessingFeatureSourceDefinition fromVar = qvariant_cast<QgsProcessingFeatureSourceDefinition>( fsInVariant );
  QCOMPARE( fromVar.source.staticValue().toString(), sourceString );
  QVERIFY( fromVar.selectedFeaturesOnly );

  // test evaluating parameter as source
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  QgsFeature f( 10001 );
  f.setGeometry( QgsGeometry( new QgsPoint( 1, 2 ) ) );
  layer->dataProvider()->addFeatures( QgsFeatureList() << f );

  QgsProject p;
  p.addMapLayer( layer );
  QgsProcessingContext context;
  context.setProject( &p );

  // first using static string definition
  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), QgsProcessingFeatureSourceDefinition( layer->id(), false ) );
  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QgsFeature f2;
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry(), f.geometry() );

  // direct map layer
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( layer ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry(), f.geometry() );


  // next using property based definition
  params.insert( QStringLiteral( "layer" ), QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( QStringLiteral( "trim('%1' + ' ')" ).arg( layer->id() ) ), false ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry(), f.geometry() );
}

void TestQgsProcessing::processingFeatureSink()
{
  QString sinkString( QStringLiteral( "test.shp" ) );
  QgsProject p;
  QgsProcessingOutputLayerDefinition fs( sinkString, &p );
  QCOMPARE( fs.sink.staticValue().toString(), sinkString );
  QCOMPARE( fs.destinationProject, &p );

  // test storing QgsProcessingFeatureSink in variant and retrieving
  QVariant fsInVariant = QVariant::fromValue( fs );
  QVERIFY( fsInVariant.isValid() );

  QgsProcessingOutputLayerDefinition fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( fsInVariant );
  QCOMPARE( fromVar.sink.staticValue().toString(), sinkString );
  QCOMPARE( fromVar.destinationProject, &p );

  // test evaluating parameter as sink
  QgsProcessingContext context;
  context.setProject( &p );

  // first using static string definition
  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( "memory:test", nullptr ) );
  QString dest;
  std::unique_ptr< QgsFeatureSink > sink( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3111" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );

  // next using property based definition
  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( QStringLiteral( "trim('memory' + ':test2')" ) ), nullptr ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer2 = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer2 );
  QCOMPARE( layer2->crs().authid(), QStringLiteral( "EPSG:3113" ) );


  // non optional sink
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessingParameterDefinition::TypeAny, QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( "memory:test" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QString() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  params.insert( QStringLiteral( "layer" ), QStringLiteral( "memory:test" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );

  // optional sink
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessingParameterDefinition::TypeAny, QVariant(), true ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QString() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  params.insert( QStringLiteral( "layer" ), QStringLiteral( "memory:test" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );
  // optional sink, not set - should be no sink
  params.insert( QStringLiteral( "layer" ), QVariant() );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( !sink.get() );

  //.... unless there's a default set
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessingParameterDefinition::TypeAny, QStringLiteral( "memory:defaultlayer" ), true ) );
  params.insert( QStringLiteral( "layer" ), QVariant() );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );
}

void TestQgsProcessing::algorithmScope()
{
  QgsProcessingContext pc;

  // no alg
  std::unique_ptr< QgsExpressionContextScope > scope( QgsExpressionContextUtils::processingAlgorithmScope( nullptr, QVariantMap(), pc ) );
  QVERIFY( scope.get() );

  // with alg
  std::unique_ptr< QgsProcessingAlgorithm > alg( new DummyAlgorithm( "alg1" ) );
  QVariantMap params;
  params.insert( QStringLiteral( "a_param" ), 5 );
  scope.reset( QgsExpressionContextUtils::processingAlgorithmScope( alg.get(), params, pc ) );
  QVERIFY( scope.get() );
  QCOMPARE( scope->variable( QStringLiteral( "algorithm_id" ) ).toString(), alg->id() );

  QgsExpressionContext context;
  context.appendScope( scope.release() );
  QgsExpression exp( "parameter('bad')" );
  QVERIFY( !exp.evaluate( &context ).isValid() );
  QgsExpression exp2( "parameter('a_param')" );
  QCOMPARE( exp2.evaluate( &context ).toInt(), 5 );
}

void TestQgsProcessing::validateInputCrs()
{
  DummyAlgorithm alg( "test" );
  alg.runValidateInputCrsChecks();
}

void TestQgsProcessing::generateIteratingDestination()
{
  QgsProcessingContext context;
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "memory:x", 1, context ).toString(), QStringLiteral( "memory:x_1" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "memory:x", 2, context ).toString(), QStringLiteral( "memory:x_2" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "ape.shp", 1, context ).toString(), QStringLiteral( "ape_1.shp" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "ape.shp", 2, context ).toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "/home/bif.o/ape.shp", 2, context ).toString(), QStringLiteral( "/home/bif.o/ape_2.shp" ) );

  QgsProject p;
  QgsProcessingOutputLayerDefinition def;
  def.sink = QgsProperty::fromValue( "ape.shp" );
  def.destinationProject = &p;
  QVariant res = QgsProcessingUtils::generateIteratingDestination( def, 2, context );
  QVERIFY( res.canConvert<QgsProcessingOutputLayerDefinition>() );
  QgsProcessingOutputLayerDefinition fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( res );
  QCOMPARE( fromVar.sink.staticValue().toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( fromVar.destinationProject, &p );

  def.sink = QgsProperty::fromExpression( "'ape' || '.shp'" );
  res = QgsProcessingUtils::generateIteratingDestination( def, 2, context );
  QVERIFY( res.canConvert<QgsProcessingOutputLayerDefinition>() );
  fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( res );
  QCOMPARE( fromVar.sink.staticValue().toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( fromVar.destinationProject, &p );
}

void TestQgsProcessing::asPythonCommand()
{
  DummyAlgorithm alg( "test" );
  alg.runAsPythonCommandChecks();
}

void TestQgsProcessing::modelerAlgorithm()
{
  //static value source
  QgsProcessingModelAlgorithm::ChildParameterSource svSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 );
  QCOMPARE( svSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  QCOMPARE( svSource.staticValue().toInt(), 5 );
  svSource.setStaticValue( 7 );
  QCOMPARE( svSource.staticValue().toInt(), 7 );
  svSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "a" );
  // check that calling setStaticValue flips source to StaticValue
  QCOMPARE( svSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ModelParameter );
  svSource.setStaticValue( 7 );
  QCOMPARE( svSource.staticValue().toInt(), 7 );
  QCOMPARE( svSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );

  // model parameter source
  QgsProcessingModelAlgorithm::ChildParameterSource mpSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "a" );
  QCOMPARE( mpSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ModelParameter );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "a" ) );
  mpSource.setParameterName( "b" );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "b" ) );
  mpSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 );
  // check that calling setParameterName flips source to ModelParameter
  QCOMPARE( mpSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  mpSource.setParameterName( "c" );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "c" ) );
  QCOMPARE( mpSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ModelParameter );

  // child alg output source
  QgsProcessingModelAlgorithm::ChildParameterSource oSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "a", "b" );
  QCOMPARE( oSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ChildOutput );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "a" ) );
  QCOMPARE( oSource.outputName(), QStringLiteral( "b" ) );
  oSource.setOutputChildId( "c" );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "c" ) );
  oSource.setOutputName( "d" );
  QCOMPARE( oSource.outputName(), QStringLiteral( "d" ) );
  oSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 );
  // check that calling setOutputChildId flips source to ChildOutput
  QCOMPARE( oSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  oSource.setOutputChildId( "c" );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "c" ) );
  QCOMPARE( oSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ChildOutput );
  oSource = QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 );
  // check that calling setOutputName flips source to ChildOutput
  QCOMPARE( oSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  oSource.setOutputName( "d" );
  QCOMPARE( oSource.outputName(), QStringLiteral( "d" ) );
  QCOMPARE( oSource.source(), QgsProcessingModelAlgorithm::ChildParameterSource::ChildOutput );

  // source equality operator
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) ==
           QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 7 ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) ==
           QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "b" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) ==
           QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg2" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) !=
           QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out2" ) ) );





  QgsProcessingModelAlgorithm::ChildAlgorithm child( QStringLiteral( "some_id" ) );
  QCOMPARE( child.algorithmId(), QStringLiteral( "some_id" ) );
  QVERIFY( !child.algorithm() );
  child.setAlgorithmId( QStringLiteral( "native:centroids" ) );
  QVERIFY( child.algorithm() );
  QCOMPARE( child.algorithm()->id(), QStringLiteral( "native:centroids" ) );
  child.setDescription( QStringLiteral( "desc" ) );
  QCOMPARE( child.description(), QStringLiteral( "desc" ) );
  QVERIFY( child.isActive() );
  child.setActive( false );
  QVERIFY( !child.isActive() );
  child.setPosition( QPointF( 1, 2 ) );
  QCOMPARE( child.position(), QPointF( 1, 2 ) );
  QVERIFY( child.parametersCollapsed() );
  child.setParametersCollapsed( false );
  QVERIFY( !child.parametersCollapsed() );
  QVERIFY( child.outputsCollapsed() );
  child.setOutputsCollapsed( false );
  QVERIFY( !child.outputsCollapsed() );

  child.setChildId( QStringLiteral( "my_id" ) );
  QCOMPARE( child.childId(), QStringLiteral( "my_id" ) );

  child.setDependencies( QStringList() << "a" << "b" );
  QCOMPARE( child.dependencies(), QStringList() << "a" << "b" );

  QMap< QString, QgsProcessingModelAlgorithm::ChildParameterSources > sources;
  sources.insert( QStringLiteral( "a" ), QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) );
  child.setParameterSources( sources );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "a" ) ).at( 0 ).staticValue().toInt(), 5 );
  child.addParameterSources( QStringLiteral( "b" ), QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 7 ) << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 9 ) );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "a" ) ).at( 0 ).staticValue().toInt(), 5 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).count(), 2 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).at( 0 ).staticValue().toInt(), 7 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).at( 1 ).staticValue().toInt(), 9 );

  QgsProcessingModelAlgorithm::ModelOutput testModelOut;
  testModelOut.setChildId( QStringLiteral( "my_id" ) );
  QCOMPARE( testModelOut.childId(), QStringLiteral( "my_id" ) );
  testModelOut.setChildOutputName( QStringLiteral( "my_output" ) );
  QCOMPARE( testModelOut.childOutputName(), QStringLiteral( "my_output" ) );

  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> outputs;
  QgsProcessingModelAlgorithm::ModelOutput out1;
  out1.setDescription( QStringLiteral( "my output" ) );
  outputs.insert( QStringLiteral( "a" ), out1 );
  child.setModelOutputs( outputs );
  QCOMPARE( child.modelOutputs().count(), 1 );
  QCOMPARE( child.modelOutputs().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( child.modelOutput( "a" ).description(), QStringLiteral( "my output" ) );
  child.modelOutput( "a" ).setDescription( QStringLiteral( "my output 2" ) );
  QCOMPARE( child.modelOutput( "a" ).description(), QStringLiteral( "my output 2" ) );
  // no existent
  child.modelOutput( "b" ).setDescription( QStringLiteral( "my output 3" ) );
  QCOMPARE( child.modelOutput( "b" ).description(), QStringLiteral( "my output 3" ) );
  QCOMPARE( child.modelOutputs().count(), 2 );



  // model algorithm tests


  QgsProcessingModelAlgorithm alg( "test", "testGroup" );
  QCOMPARE( alg.name(), QStringLiteral( "test" ) );
  QCOMPARE( alg.displayName(), QStringLiteral( "test" ) );
  QCOMPARE( alg.group(), QStringLiteral( "testGroup" ) );
  alg.setName( QStringLiteral( "test2" ) );
  QCOMPARE( alg.name(), QStringLiteral( "test2" ) );
  QCOMPARE( alg.displayName(), QStringLiteral( "test2" ) );
  alg.setGroup( QStringLiteral( "group2" ) );
  QCOMPARE( alg.group(), QStringLiteral( "group2" ) );

  // child algorithms
  QMap<QString, QgsProcessingModelAlgorithm::ChildAlgorithm> algs;
  QgsProcessingModelAlgorithm::ChildAlgorithm a1;
  a1.setDescription( QStringLiteral( "alg1" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm a2;
  a2.setDescription( QStringLiteral( "alg2" ) );
  algs.insert( QStringLiteral( "a" ), a1 );
  algs.insert( QStringLiteral( "b" ), a2 );
  alg.setChildAlgorithms( algs );
  QCOMPARE( alg.childAlgorithms().count(), 2 );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "b" ) ).description(), QStringLiteral( "alg2" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm a3;
  a3.setChildId( QStringLiteral( "c" ) );
  a3.setDescription( QStringLiteral( "alg3" ) );
  QCOMPARE( alg.addChildAlgorithm( a3 ), QStringLiteral( "c" ) );
  QCOMPARE( alg.childAlgorithms().count(), 3 );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "b" ) ).description(), QStringLiteral( "alg2" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "c" ) ).description(), QStringLiteral( "alg3" ) );
  QCOMPARE( alg.childAlgorithm( "a" ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithm( "b" ).description(), QStringLiteral( "alg2" ) );
  QCOMPARE( alg.childAlgorithm( "c" ).description(), QStringLiteral( "alg3" ) );
  // initially non-existent
  QVERIFY( alg.childAlgorithm( "d" ).description().isEmpty() );
  alg.childAlgorithm( "d" ).setDescription( QStringLiteral( "alg4" ) );
  QCOMPARE( alg.childAlgorithm( "d" ).description(), QStringLiteral( "alg4" ) );
  // overwrite existing
  QgsProcessingModelAlgorithm::ChildAlgorithm a4a;
  a4a.setChildId( "d" );
  a4a.setDescription( "new" );
  alg.setChildAlgorithm( a4a );
  QCOMPARE( alg.childAlgorithm( "d" ).description(), QStringLiteral( "new" ) );

  // generating child ids
  QgsProcessingModelAlgorithm::ChildAlgorithm c1;
  c1.setAlgorithmId( QStringLiteral( "buffer" ) );
  c1.generateChildId( alg );
  QCOMPARE( c1.childId(), QStringLiteral( "buffer_1" ) );
  QCOMPARE( alg.addChildAlgorithm( c1 ), QStringLiteral( "buffer_1" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm c2;
  c2.setAlgorithmId( QStringLiteral( "buffer" ) );
  c2.generateChildId( alg );
  QCOMPARE( c2.childId(), QStringLiteral( "buffer_2" ) );
  QCOMPARE( alg.addChildAlgorithm( c2 ), QStringLiteral( "buffer_2" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm c3;
  c3.setAlgorithmId( QStringLiteral( "centroid" ) );
  c3.generateChildId( alg );
  QCOMPARE( c3.childId(), QStringLiteral( "centroid_1" ) );
  QCOMPARE( alg.addChildAlgorithm( c3 ), QStringLiteral( "centroid_1" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm c4;
  c4.setAlgorithmId( QStringLiteral( "centroid" ) );
  c4.setChildId( QStringLiteral( "centroid_1" ) );// dupe id
  QCOMPARE( alg.addChildAlgorithm( c4 ), QStringLiteral( "centroid_2" ) );
  QCOMPARE( alg.childAlgorithm( QStringLiteral( "centroid_2" ) ).childId(), QStringLiteral( "centroid_2" ) );

  // parameter components
  QMap<QString, QgsProcessingModelAlgorithm::ModelParameter> pComponents;
  QgsProcessingModelAlgorithm::ModelParameter pc1;
  pc1.setParameterName( QStringLiteral( "my_param" ) );
  QCOMPARE( pc1.parameterName(), QStringLiteral( "my_param" ) );
  pComponents.insert( QStringLiteral( "my_param" ), pc1 );
  alg.setParameterComponents( pComponents );
  QCOMPARE( alg.parameterComponents().count(), 1 );
  QCOMPARE( alg.parameterComponents().value( QStringLiteral( "my_param" ) ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg.parameterComponent( "my_param" ).parameterName(), QStringLiteral( "my_param" ) );
  alg.parameterComponent( "my_param" ).setDescription( QStringLiteral( "my param 2" ) );
  QCOMPARE( alg.parameterComponent( "my_param" ).description(), QStringLiteral( "my param 2" ) );
  // no existent
  alg.parameterComponent( "b" ).setDescription( QStringLiteral( "my param 3" ) );
  QCOMPARE( alg.parameterComponent( "b" ).description(), QStringLiteral( "my param 3" ) );
  QCOMPARE( alg.parameterComponent( "b" ).parameterName(), QStringLiteral( "b" ) );
  QCOMPARE( alg.parameterComponents().count(), 2 );

  // parameter definitions
  QgsProcessingModelAlgorithm alg1a( "test", "testGroup" );
  QgsProcessingModelAlgorithm::ModelParameter bool1;
  bool1.setPosition( QPointF( 1, 2 ) );
  alg1a.addModelParameter( new QgsProcessingParameterBoolean( "p1", "desc" ), bool1 );
  QCOMPARE( alg1a.parameterDefinitions().count(), 1 );
  QCOMPARE( alg1a.parameterDefinition( "p1" )->type(), QStringLiteral( "boolean" ) );
  QCOMPARE( alg1a.parameterComponent( "p1" ).position().x(), 1.0 );
  QCOMPARE( alg1a.parameterComponent( "p1" ).position().y(), 2.0 );
  alg1a.updateModelParameter( new QgsProcessingParameterBoolean( "p1", "descx" ) );
  QCOMPARE( alg1a.parameterDefinition( "p1" )->description(), QStringLiteral( "descx" ) );
  alg1a.removeModelParameter( "bad" );
  QCOMPARE( alg1a.parameterDefinitions().count(), 1 );
  alg1a.removeModelParameter( "p1" );
  QVERIFY( alg1a.parameterDefinitions().isEmpty() );
  QVERIFY( alg1a.parameterComponents().isEmpty() );


  // test canExecute
  QgsProcessingModelAlgorithm alg2( "test", "testGroup" );
  QVERIFY( alg2.canExecute() );
  QgsProcessingModelAlgorithm::ChildAlgorithm c5;
  c5.setAlgorithmId( "native:centroids" );
  alg2.addChildAlgorithm( c5 );
  QVERIFY( alg2.canExecute() );
  // non-existing alg
  QgsProcessingModelAlgorithm::ChildAlgorithm c6;
  c6.setAlgorithmId( "i'm not an alg" );
  alg2.addChildAlgorithm( c6 );
  QVERIFY( !alg2.canExecute() );



  // dependencies
  QgsProcessingModelAlgorithm alg3( "test", "testGroup" );
  QVERIFY( alg3.dependentChildAlgorithms( "notvalid" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "notvalid" ).isEmpty() );

  // add a child
  QgsProcessingModelAlgorithm::ChildAlgorithm c7;
  c7.setChildId( "c7" );
  alg3.addChildAlgorithm( c7 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );

  // direct dependency
  QgsProcessingModelAlgorithm::ChildAlgorithm c8;
  c8.setChildId( "c8" );
  c8.setDependencies( QStringList() << "c7" );
  alg3.addChildAlgorithm( c8 );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QCOMPARE( alg3.dependentChildAlgorithms( "c7" ).count(), 1 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c8" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c8" ).contains( "c7" ) );

  // dependency via parameter source
  QgsProcessingModelAlgorithm::ChildAlgorithm c9;
  c9.setChildId( "c9" );
  c9.addParameterSources( "x", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "c8", "x" ) );
  alg3.addChildAlgorithm( c9 );
  QVERIFY( alg3.dependentChildAlgorithms( "c9" ).isEmpty() );
  QCOMPARE( alg3.dependentChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).contains( "c9" ) );
  QCOMPARE( alg3.dependentChildAlgorithms( "c7" ).count(), 2 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c8" ) );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c9" ) );

  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c8" ).contains( "c7" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c9" ).count(), 2 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c7" ) );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c8" ) );

  // (de)activate child algorithm
  alg3.deactivateChildAlgorithm( "c9" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  alg3.deactivateChildAlgorithm( "c8" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  alg3.deactivateChildAlgorithm( "c7" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( !alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( alg3.activateChildAlgorithm( "c7" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );



  //remove child algorithm
  QVERIFY( !alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( !alg3.removeChildAlgorithm( "c8" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c9" ) );
  QCOMPARE( alg3.childAlgorithms().count(), 2 );
  QVERIFY( alg3.childAlgorithms().contains( "c7" ) );
  QVERIFY( alg3.childAlgorithms().contains( "c8" ) );
  QVERIFY( !alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c8" ) );
  QCOMPARE( alg3.childAlgorithms().count(), 1 );
  QVERIFY( alg3.childAlgorithms().contains( "c7" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( alg3.childAlgorithms().isEmpty() );

  // parameter dependencies
  QgsProcessingModelAlgorithm alg4( "test", "testGroup" );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "not a param" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm c10;
  c10.setChildId( "c10" );
  alg4.addChildAlgorithm( c10 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "not a param" ) );
  QgsProcessingModelAlgorithm::ModelParameter bool2;
  alg4.addModelParameter( new QgsProcessingParameterBoolean( "p1", "desc" ), bool2 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "p1" ) );
  c10.addParameterSources( "x", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "p2" ) );
  alg4.setChildAlgorithm( c10 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "p1" ) );
  c10.addParameterSources( "y", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "p1" ) );
  alg4.setChildAlgorithm( c10 );
  QVERIFY( alg4.childAlgorithmsDependOnParameter( "p1" ) );

  QgsProcessingModelAlgorithm::ModelParameter vlP;
  alg4.addModelParameter( new QgsProcessingParameterVectorLayer( "layer" ), vlP );
  QgsProcessingModelAlgorithm::ModelParameter field;
  alg4.addModelParameter( new QgsProcessingParameterField( "field", QString(), QVariant(), QStringLiteral( "layer" ) ), field );
  QVERIFY( !alg4.otherParametersDependOnParameter( "p1" ) );
  QVERIFY( !alg4.otherParametersDependOnParameter( "field" ) );
  QVERIFY( alg4.otherParametersDependOnParameter( "layer" ) );





  // to/from XML
  QgsProcessingModelAlgorithm alg5( "test", "testGroup" );
  alg5.helpContent().insert( "author", "me" );
  alg5.helpContent().insert( "usage", "run" );
  QgsProcessingModelAlgorithm::ChildAlgorithm alg5c1;
  alg5c1.setChildId( "cx1" );
  alg5c1.setAlgorithmId( "buffer" );
  alg5c1.addParameterSources( "x", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "p1" ) );
  alg5c1.addParameterSources( "y", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx2", "out3" ) );
  alg5c1.addParameterSources( "z", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 5 ) );
  alg5c1.addParameterSources( "zm", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 6 )
                              << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "p2" )
                              << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx2", "out4" ) );
  alg5c1.setActive( true );
  alg5c1.setOutputsCollapsed( true );
  alg5c1.setParametersCollapsed( true );
  alg5c1.setDescription( "child 1" );
  alg5c1.setPosition( QPointF( 1, 2 ) );
  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> alg5c1outputs;
  QgsProcessingModelAlgorithm::ModelOutput alg5c1out1;
  alg5c1out1.setDescription( QStringLiteral( "my output" ) );
  alg5c1out1.setPosition( QPointF( 3, 4 ) );
  alg5c1outputs.insert( QStringLiteral( "a" ), alg5c1out1 );
  alg5c1.setModelOutputs( alg5c1outputs );
  alg5.addChildAlgorithm( alg5c1 );

  QgsProcessingModelAlgorithm::ChildAlgorithm alg5c2;
  alg5c2.setChildId( "cx2" );
  alg5c2.setActive( false );
  alg5c2.setOutputsCollapsed( false );
  alg5c2.setParametersCollapsed( false );
  alg5c2.setDependencies( QStringList() << "a" << "b" );
  alg5.addChildAlgorithm( alg5c2 );

  QgsProcessingModelAlgorithm::ModelParameter alg5pc1;
  alg5pc1.setParameterName( QStringLiteral( "my_param" ) );
  alg5pc1.setPosition( QPointF( 11, 12 ) );
  alg5.addModelParameter( new QgsProcessingParameterBoolean( QStringLiteral( "my_param" ) ), alg5pc1 );

  QDomDocument doc = QDomDocument( "model" );
  QDomElement elem = QgsXmlUtils::writeVariant( alg5.toVariant(), doc );
  doc.appendChild( elem );

  QgsProcessingModelAlgorithm alg6;
  QVERIFY( alg6.loadVariant( QgsXmlUtils::readVariant( doc.firstChildElement() ) ) );
  QCOMPARE( alg6.name(), QStringLiteral( "test" ) );
  QCOMPARE( alg6.group(), QStringLiteral( "testGroup" ) );
  QCOMPARE( alg6.helpContent(), alg5.helpContent() );
  QgsProcessingModelAlgorithm::ChildAlgorithm alg6c1 = alg6.childAlgorithm( "cx1" );
  QCOMPARE( alg6c1.childId(), QStringLiteral( "cx1" ) );
  QCOMPARE( alg6c1.algorithmId(), QStringLiteral( "buffer" ) );
  QVERIFY( alg6c1.isActive() );
  QVERIFY( alg6c1.outputsCollapsed() );
  QVERIFY( alg6c1.parametersCollapsed() );
  QCOMPARE( alg6c1.description(), QStringLiteral( "child 1" ) );
  QCOMPARE( alg6c1.position().x(), 1.0 );
  QCOMPARE( alg6c1.position().y(), 2.0 );
  QCOMPARE( alg6c1.parameterSources().count(), 4 );
  QCOMPARE( alg6c1.parameterSources().value( "x" ).at( 0 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::ModelParameter );
  QCOMPARE( alg6c1.parameterSources().value( "x" ).at( 0 ).parameterName(), QStringLiteral( "p1" ) );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::ChildOutput );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).outputName(), QStringLiteral( "out3" ) );
  QCOMPARE( alg6c1.parameterSources().value( "z" ).at( 0 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  QCOMPARE( alg6c1.parameterSources().value( "z" ).at( 0 ).staticValue().toInt(), 5 );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 0 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::StaticValue );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 0 ).staticValue().toInt(), 6 );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 1 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::ModelParameter );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 1 ).parameterName(), QStringLiteral( "p2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).source(), QgsProcessingModelAlgorithm::ChildParameterSource::ChildOutput );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).outputName(), QStringLiteral( "out4" ) );

  QCOMPARE( alg6c1.modelOutputs().count(), 1 );
  QCOMPARE( alg6c1.modelOutputs().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg6c1.modelOutput( "a" ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg6c1.modelOutput( "a" ).position().x(), 3.0 );
  QCOMPARE( alg6c1.modelOutput( "a" ).position().y(), 4.0 );


  QgsProcessingModelAlgorithm::ChildAlgorithm alg6c2 = alg6.childAlgorithm( "cx2" );
  QCOMPARE( alg6c2.childId(), QStringLiteral( "cx2" ) );
  QVERIFY( !alg6c2.isActive() );
  QVERIFY( !alg6c2.outputsCollapsed() );
  QVERIFY( !alg6c2.parametersCollapsed() );
  QCOMPARE( alg6c2.dependencies(), QStringList() << "a" << "b" );

  QCOMPARE( alg6.parameterComponents().count(), 1 );
  QCOMPARE( alg6.parameterComponents().value( QStringLiteral( "my_param" ) ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg6.parameterComponent( "my_param" ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg6.parameterComponent( "my_param" ).position().x(), 11.0 );
  QCOMPARE( alg6.parameterComponent( "my_param" ).position().y(), 12.0 );
  QCOMPARE( alg6.parameterDefinitions().count(), 1 );
  QCOMPARE( alg6.parameterDefinitions().at( 0 )->type(), QStringLiteral( "boolean" ) );

  // destination parameters
  QgsProcessingModelAlgorithm alg7( "test", "testGroup" );
  QgsProcessingModelAlgorithm::ChildAlgorithm alg7c1;
  alg7c1.setChildId( "cx1" );
  alg7c1.setAlgorithmId( "native:centroids" );
  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> alg7c1outputs;
  QgsProcessingModelAlgorithm::ModelOutput alg7c1out1( QStringLiteral( "my_output" ) );
  alg7c1out1.setChildId( "cx1" );
  alg7c1out1.setChildOutputName( "OUTPUT_LAYER" );
  alg7c1out1.setDescription( QStringLiteral( "my output" ) );
  alg7c1outputs.insert( QStringLiteral( "my_output" ), alg7c1out1 );
  alg7c1.setModelOutputs( alg7c1outputs );
  alg7.addChildAlgorithm( alg7c1 );
  // verify that model has destination parameter created
  QCOMPARE( alg7.destinationParameterDefinitions().count(), 1 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg7.outputDefinitions().count(), 1 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );

  QgsProcessingModelAlgorithm::ChildAlgorithm alg7c2;
  alg7c2.setChildId( "cx2" );
  alg7c2.setAlgorithmId( "native:centroids" );
  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> alg7c2outputs;
  QgsProcessingModelAlgorithm::ModelOutput alg7c2out1( QStringLiteral( "my_output2" ) );
  alg7c2out1.setChildId( "cx2" );
  alg7c2out1.setChildOutputName( "OUTPUT_LAYER" );
  alg7c2out1.setDescription( QStringLiteral( "my output2" ) );
  alg7c2outputs.insert( QStringLiteral( "my_output2" ), alg7c2out1 );
  alg7c2.setModelOutputs( alg7c2outputs );
  alg7.addChildAlgorithm( alg7c2 );

  QCOMPARE( alg7.destinationParameterDefinitions().count(), 2 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 1 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 1 )->description(), QStringLiteral( "my output2" ) );
  QCOMPARE( alg7.outputDefinitions().count(), 2 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->description(), QStringLiteral( "my output2" ) );

  alg7.removeChildAlgorithm( "cx1" );
  QCOMPARE( alg7.destinationParameterDefinitions().count(), 1 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output2" ) );
  QCOMPARE( alg7.outputDefinitions().count(), 1 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output2" ) );
}

void TestQgsProcessing::modelExecution()
{
  // test childOutputIsRequired
  QgsProcessingModelAlgorithm model1;
  QgsProcessingModelAlgorithm::ChildAlgorithm algc1;
  algc1.setChildId( "cx1" );
  algc1.setAlgorithmId( "native:centroids" );
  model1.addChildAlgorithm( algc1 );
  QgsProcessingModelAlgorithm::ChildAlgorithm algc2;
  algc2.setChildId( "cx2" );
  algc2.setAlgorithmId( "native:centroids" );
  algc2.addParameterSources( "x", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx1", "p1" ) );
  model1.addChildAlgorithm( algc2 );
  QgsProcessingModelAlgorithm::ChildAlgorithm algc3;
  algc3.setChildId( "cx3" );
  algc3.setAlgorithmId( "native:centroids" );
  algc3.addParameterSources( "x", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx1", "p2" ) );
  algc3.setActive( false );
  model1.addChildAlgorithm( algc3 );

  QVERIFY( model1.childOutputIsRequired( "cx1", "p1" ) ); // cx2 depends on p1
  QVERIFY( !model1.childOutputIsRequired( "cx1", "p2" ) ); // cx3 depends on p2, but cx3 is not active
  QVERIFY( !model1.childOutputIsRequired( "cx1", "p3" ) ); // nothing requires p3
  QVERIFY( !model1.childOutputIsRequired( "cx2", "p1" ) );
  QVERIFY( !model1.childOutputIsRequired( "cx3", "p1" ) );

  // test parametersForChildAlgorithm
  QgsProcessingModelAlgorithm model2;
  model2.addModelParameter( new QgsProcessingParameterFeatureSource( "SOURCE_LAYER" ), QgsProcessingModelAlgorithm::ModelParameter( "SOURCE_LAYER" ) );
  model2.addModelParameter( new QgsProcessingParameterNumber( "DIST", QString(), QgsProcessingParameterNumber::Double ), QgsProcessingModelAlgorithm::ModelParameter( "DIST" ) );
  QgsProcessingModelAlgorithm::ChildAlgorithm alg2c1;
  alg2c1.setChildId( "cx1" );
  alg2c1.setAlgorithmId( "native:buffer" );
  alg2c1.addParameterSources( "INPUT", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "SOURCE_LAYER" ) );
  alg2c1.addParameterSources( "DISTANCE", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "DIST" ) );
  alg2c1.addParameterSources( "SEGMENTS", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 16 ) );
  alg2c1.addParameterSources( "END_CAP_STYLE", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 1 ) );
  alg2c1.addParameterSources( "JOIN_STYLE", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( 2 ) );
  alg2c1.addParameterSources( "DISSOLVE", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( false ) );
  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> outputs1;
  QgsProcessingModelAlgorithm::ModelOutput out1( "MODEL_OUT_LAYER" );
  out1.setChildOutputName( "OUTPUT_LAYER" );
  outputs1.insert( QStringLiteral( "MODEL_OUT_LAYER" ), out1 );
  alg2c1.setModelOutputs( outputs1 );
  model2.addChildAlgorithm( alg2c1 );

  QVariantMap modelInputs;
  modelInputs.insert( "SOURCE_LAYER", "my_layer_id" );
  modelInputs.insert( "DIST", 271 );
  modelInputs.insert( "cx1:MODEL_OUT_LAYER", "dest.shp" );
  QgsProcessingOutputLayerDefinition layerDef( "memory:" );
  layerDef.destinationName = "my_dest";
  modelInputs.insert( "cx3:MY_OUT", QVariant::fromValue( layerDef ) );
  QMap<QString, QVariantMap> childResults;
  QVariantMap params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx1" ), modelInputs, childResults );
  QCOMPARE( params.value( "DISSOLVE" ).toBool(), false );
  QCOMPARE( params.value( "DISTANCE" ).toInt(), 271 );
  QCOMPARE( params.value( "SEGMENTS" ).toInt(), 16 );
  QCOMPARE( params.value( "END_CAP_STYLE" ).toInt(), 1 );
  QCOMPARE( params.value( "JOIN_STYLE" ).toInt(), 2 );
  QCOMPARE( params.value( "INPUT" ).toString(), QStringLiteral( "my_layer_id" ) );
  QCOMPARE( params.value( "OUTPUT_LAYER" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.count(), 7 );

  QVariantMap results;
  results.insert( "OUTPUT_LAYER", QStringLiteral( "dest.shp" ) );
  childResults.insert( "cx1", results );

  // a child who uses an output from another alg as a parameter value
  QgsProcessingModelAlgorithm::ChildAlgorithm alg2c2;
  alg2c2.setChildId( "cx2" );
  alg2c2.setAlgorithmId( "native:centroids" );
  alg2c2.addParameterSources( "INPUT", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx1", "OUTPUT_LAYER" ) );
  model2.addChildAlgorithm( alg2c2 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx2" ), modelInputs, childResults );
  QCOMPARE( params.value( "INPUT" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.value( "OUTPUT_LAYER" ).toString(), QStringLiteral( "memory:" ) );
  QCOMPARE( params.count(), 2 );

  // a child with an optional output
  QgsProcessingModelAlgorithm::ChildAlgorithm alg2c3;
  alg2c3.setChildId( "cx3" );
  alg2c3.setAlgorithmId( "native:extractbyexpression" );
  alg2c3.addParameterSources( "INPUT", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromChildOutput( "cx1", "OUTPUT_LAYER" ) );
  alg2c3.addParameterSources( "EXPRESSION", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromStaticValue( "true" ) );
  alg2c3.addParameterSources( "OUTPUT", QgsProcessingModelAlgorithm::ChildParameterSources() << QgsProcessingModelAlgorithm::ChildParameterSource::fromModelParameter( "MY_OUT" ) );
  alg2c3.setDependencies( QStringList() << "cx2" );
  QMap<QString, QgsProcessingModelAlgorithm::ModelOutput> outputs3;
  QgsProcessingModelAlgorithm::ModelOutput out2( "MY_OUT" );
  out2.setChildOutputName( "OUTPUT" );
  outputs3.insert( QStringLiteral( "MY_OUT" ), out2 );
  alg2c3.setModelOutputs( outputs3 );

  model2.addChildAlgorithm( alg2c3 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx3" ), modelInputs, childResults );
  QCOMPARE( params.value( "INPUT" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.value( "EXPRESSION" ).toString(), QStringLiteral( "true" ) );
  QVERIFY( params.value( "OUTPUT" ).canConvert<QgsProcessingOutputLayerDefinition>() );
  QgsProcessingOutputLayerDefinition outDef = qvariant_cast<QgsProcessingOutputLayerDefinition>( params.value( "OUTPUT" ) );
  QCOMPARE( outDef.destinationName, QStringLiteral( "MY_OUT" ) );
  QCOMPARE( outDef.sink.staticValue().toString(), QStringLiteral( "memory:" ) );
  QCOMPARE( params.count(), 3 ); // don't want FAIL_OUTPUT set!

  QStringList actualParts = model2.asPythonCode().split( '\n' );
  QStringList expectedParts = QStringLiteral( "##model=name\n"
                              "##DIST=number\n"
                              "##SOURCE_LAYER=source\n"
                              "##model_out_layer=output outputVector\n"
                              "##my_out=output outputVector\n"
                              "results={}\n"
                              "outputs['cx1']=processing.run('native:buffer', {'DISSOLVE':false,'DISTANCE':parameters['DIST'],'END_CAP_STYLE':1,'INPUT':parameters['SOURCE_LAYER'],'JOIN_STYLE':2,'SEGMENTS':16}, context=context, feedback=feedback)\n"
                              "results['MODEL_OUT_LAYER']=outputs['cx1']['OUTPUT_LAYER']\n"
                              "outputs['cx2']=processing.run('native:centroids', {'INPUT':outputs['cx1']['OUTPUT_LAYER']}, context=context, feedback=feedback)\n"
                              "outputs['cx3']=processing.run('native:extractbyexpression', {'EXPRESSION':true,'INPUT':outputs['cx1']['OUTPUT_LAYER'],'OUTPUT':parameters['MY_OUT']}, context=context, feedback=feedback)\n"
                              "results['MY_OUT']=outputs['cx3']['OUTPUT']\n"
                              "return results" ).split( '\n' );
  QCOMPARE( actualParts, expectedParts );
}

void TestQgsProcessing::tempUtils()
{
  QString tempFolder = QgsProcessingUtils::tempFolder();
  // tempFolder should remain constant for session
  QCOMPARE( QgsProcessingUtils::tempFolder(), tempFolder );

  QString tempFile1 = QgsProcessingUtils::generateTempFilename( "test.txt" );
  QVERIFY( tempFile1.endsWith( "test.txt" ) );
  QVERIFY( tempFile1.startsWith( tempFolder ) );

  // expect a different file
  QString tempFile2 = QgsProcessingUtils::generateTempFilename( "test.txt" );
  QVERIFY( tempFile1 != tempFile2 );
  QVERIFY( tempFile2.endsWith( "test.txt" ) );
  QVERIFY( tempFile2.startsWith( tempFolder ) );
}

QGSTEST_MAIN( TestQgsProcessing )
#include "testqgsprocessing.moc"
