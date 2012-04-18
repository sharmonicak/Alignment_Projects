

#include	"PipeFiles.h"

#include	"CTileSet.h"
#include	"File.h"
#include	"ImageIO.h"
#include	"Maths.h"
#include	"Geometry.h"

#include	"tinyxml.h"


/* --------------------------------------------------------------- */
/* Statics ------------------------------------------------------- */
/* --------------------------------------------------------------- */

static CUTile	*_Til;
static TSAux	*_Aux;






/* --------------------------------------------------------------- */
/* SetDecoderPat ------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::SetDecoderPat( const char *pat )
{
	re_id.Set( pat );
	re_id.Compile( flog );
}

/* --------------------------------------------------------------- */
/* DecodeID ------------------------------------------------------ */
/* --------------------------------------------------------------- */

int CTileSet::DecodeID( const char *name )
{
#if 0
// (Temporary) hack for Davi with no id in filename

	static int	stile=0;
	return stile++;

#else
// Standard method to extract id from filename

	const char	*s = strrchr( name, '/' );
	int			id;

	if( !s ) {
		fprintf( flog, "No '/' in [%s].\n", name );
		exit( 42 );
	}

	if( !re_id.Decode( id, ++s ) ) {
		printf( "No tile-id found in [%s].\n", s );
		exit( 42 );
	}

	return id;
#endif
}

/* --------------------------------------------------------------- */
/* FillFromRickFile ---------------------------------------------- */
/* --------------------------------------------------------------- */

// Needs previous call to SetDecoderPat().
//
void CTileSet::FillFromRickFile( const char *path, int zmin, int zmax )
{
	FILE	*fp = FileOpenOrDie( path, "r", flog );

/* ---------- */
/* Scan lines */
/* ---------- */

	for( ;; ) {

		CUTile	til;
		char	name[2048];
		int		x, y, z;

		/* ---------- */
		/* Get a line */
		/* ---------- */

		if( fscanf( fp, "%s%d%d%d", name, &x, &y, &z ) != 4 )
			break;

		if( z > zmax )
			break;

		if( z < zmin )
			continue;

		/* --------- */
		/* Set entry */
		/* --------- */

		til.name	= name;
		til.z		= z;
		til.id		= DecodeID( name );
		til.T.SetXY( x, y );

		vtil.push_back( til );
	}

/* ----- */
/* Close */
/* ----- */

	fclose( fp );
}

/* --------------------------------------------------------------- */
/* FillFromTrakEM2 ----------------------------------------------- */
/* --------------------------------------------------------------- */

// Needs previous call to SetDecoderPat().
//
void CTileSet::FillFromTrakEM2( const char *path, int zmin, int zmax )
{
/* ------------- */
/* Load document */
/* ------------- */

	TiXmlDocument	doc( path );
	bool			loadOK = doc.LoadFile();

	if( !loadOK ) {
		fprintf( flog, "Could not open XML file [%s].\n", path );
		exit( 42 );
	}

/* ---------------- */
/* Verify <trakem2> */
/* ---------------- */

	TiXmlHandle		hDoc( &doc );
	TiXmlElement*	layer;

	if( !doc.FirstChild() ) {
		fprintf( flog, "No trakEM2 node.\n" );
		exit( 42 );
	}

	layer = hDoc.FirstChild( "trakem2" )
				.FirstChild( "t2_layer_set" )
				.FirstChild( "t2_layer" )
				.ToElement();

	if( !layer ) {
		fprintf( flog, "No first trakEM2 child.\n" );
		exit( 42 );
	}

/* -------------- */
/* For each layer */
/* -------------- */

	for( ; layer; layer = layer->NextSiblingElement() ) {

		/* ----------------- */
		/* Layer-level stuff */
		/* ----------------- */

		int	z = atoi( layer->Attribute( "z" ) );

		if( z > zmax )
			break;

		if( z < zmin )
			continue;

		/* ------------------------------ */
		/* For each patch (tile) in layer */
		/* ------------------------------ */

		TiXmlElement*	ptch = layer->FirstChildElement( "t2_patch" );

		for( ; ptch; ptch = ptch->NextSiblingElement() ) {

			CUTile		til;
			const char	*name = ptch->Attribute( "file_path" );

			/* --------- */
			/* Set entry */
			/* --------- */

			til.name	= name;
			til.z		= z;
			til.id		= DecodeID( name );
			til.T.ScanTrackEM2( ptch->Attribute( "transform" ) );

			vtil.push_back( til );
		}
	}
}

/* --------------------------------------------------------------- */
/* FillFromIDB --------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::FillFromIDB( const string &idb, int zmin, int zmax )
{
	for( int z = zmin; z <= zmax; ++z ) {

		vector<Til2Img>	t2i;

		if( IDBAllTil2Img( t2i, idb, z, flog ) ) {

			int	nt = t2i.size();

			for( int i = 0; i < nt; ++i ) {

				const Til2Img&	E = t2i[i];
				CUTile			til;

				til.z	= z;
				til.id	= E.tile;
				til.T	= E.T;

				vtil.push_back( til );
			}
		}
	}
}

/* --------------------------------------------------------------- */
/* SetTileDimsFromImageFile -------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::SetTileDimsFromImageFile()
{
	uint32	w, h;
	uint8	*ras = Raster8FromAny( vtil[0].name.c_str(), w, h, flog );

	if( !ras || !w ) {
		fprintf( flog, "Error loading [%s].\n", vtil[0].name.c_str() );
		exit( 42 );
	}

	RasterFree( ras );

	SetTileDims( w, h );
}

/* --------------------------------------------------------------- */
/* SetTileDimsFromIDB -------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::SetTileDimsFromIDB( const string &idb )
{
	char	buf[2048];

	sprintf( buf, "%s/imageparams.txt", idb.c_str() );
	FILE		*f = FileOpenOrDie( buf, "r" );
	CLineScan	LS;

	while( LS.Get( f ) > 0 ) {

		if( 2 == sscanf( LS.line, "IMAGESIZE %d %d", &gW, &gH ) )
			goto exit;
	}

	fprintf( flog, "IMAGESIZE tag not found.\n" );
	exit( 42 );

exit:
	fclose( f );
}

/* --------------------------------------------------------------- */
/* InitAuxData --------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::InitAuxData()
{
	int	nt = vtil.size();

	if( nt ) {

		vaux.resize( nt );

		_Aux = &vaux[0];	// for sort procs

		for( int i = 0; i < nt; ++i ) {

			vtil[i].ix	= i;
			vaux[i].r	= 0.0;
			InvertTrans( vaux[i].inv, vtil[i].T );
		}
	}
}

/* --------------------------------------------------------------- */
/* Sort_z_inc ---------------------------------------------------- */
/* --------------------------------------------------------------- */

static bool Sort_z_inc( const CUTile &A, const CUTile &B )
{
	return A.z < B.z;
}

/* --------------------------------------------------------------- */
/* Sort_id_inc --------------------------------------------------- */
/* --------------------------------------------------------------- */

static bool Sort_id_inc( int a, int b )
{
	return _Til[a].id < _Til[b].id;
}

/* --------------------------------------------------------------- */
/* Sort_z_id_inc ------------------------------------------------- */
/* --------------------------------------------------------------- */

static bool Sort_z_id_inc( const CUTile &A, const CUTile &B )
{
	return A.z < B.z || (A.z == B.z && A.id < B.id);
}

/* --------------------------------------------------------------- */
/* Sort_r_id_inc ------------------------------------------------- */
/* --------------------------------------------------------------- */

static bool Sort_r_id_inc( const CUTile &A, const CUTile &B )
{
	return _Aux[A.ix].r < _Aux[B.ix].r ||
			(_Aux[A.ix].r == _Aux[B.ix].r && A.id < B.id);
}

/* --------------------------------------------------------------- */
/* SortAll_z ----------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::SortAll_z()
{
	sort( vtil.begin(), vtil.end(), Sort_z_inc );
}

/* --------------------------------------------------------------- */
/* SortAll_z_id -------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::SortAll_z_id()
{
	sort( vtil.begin(), vtil.end(), Sort_z_id_inc );
}

/* --------------------------------------------------------------- */
/* SortAll_z_r --------------------------------------------------- */
/* --------------------------------------------------------------- */

// Within each layer/montage, sort the tiles into ascending
// order of distance from center.
//
void CTileSet::SortAll_z_r()
{
	if( !vaux.size() )
		InitAuxData();

// First, just sort into layers

	SortAll_z();

// For each layer...

	int		is0, isN;

	GetLayerLimits( is0 = 0, isN );

	while( isN != -1 ) {

		// Get montage bounds
		// Assign radii from montage center
		// Sort this layer by r

		DBox	B;

		LayerBounds( B, is0, isN );
		LayerAssignR( is0, isN, B );
		sort( vtil.begin() + is0, vtil.begin() + isN, Sort_r_id_inc );

		GetLayerLimits( is0 = isN, isN );
	}
}

/* --------------------------------------------------------------- */
/* GetOrder_id --------------------------------------------------- */
/* --------------------------------------------------------------- */

// Size and fill caller's order vector with indices of tiles
// in layer [is0,isN) and in order of tile id.
//
void CTileSet::GetOrder_id( vector<int> &order, int is0, int isN )
{
	isN -= is0;

	order.resize( isN );

	for( int i = 0; i < isN; ++i )
		order[i] = is0 + i;

	_Til = &vtil[0];

	sort( order.begin(), order.end(), Sort_id_inc );
}

/* --------------------------------------------------------------- */
/* GetLayerLimits ------------------------------------------------ */
/* --------------------------------------------------------------- */

// Tiles must already be sorted by z (subsort doesn't matter).
//
// Given starting index i0, which selects a layer z, set iN to be
// one beyond the highest index of a picture having same z. This
// makes loop limits [i0,iN) exclusive.
//
// If i0 or iN are out of bounds, both are set to -1.
//
void CTileSet::GetLayerLimits( int &i0, int &iN )
{
	int	nt = vtil.size();

	if( i0 < 0 || i0 >= nt ) {

		i0 = -1;
		iN = -1;
	}
	else {

		int	Z = vtil[i0].z;

		for( iN = i0 + 1; iN < nt && vtil[iN].z == Z; ++iN )
			;
	}
}

/* --------------------------------------------------------------- */
/* LayerBounds --------------------------------------------------- */
/* --------------------------------------------------------------- */

// Tiles must already be sorted by z (subsort doesn't matter).
//
void CTileSet::LayerBounds( DBox &B, int is0, int isN )
{
	B.L = BIGD, B.R = -BIGD,
	B.B = BIGD, B.T = -BIGD;

	for( int i = is0; i < isN; ++i ) {

		const CUTile&	U = vtil[i];
		vector<Point>	cnr( 4 );

		cnr[0] = Point(  0.0, 0.0 );
		cnr[1] = Point( gW-1, 0.0 );
		cnr[2] = Point( gW-1, gH-1 );
		cnr[3] = Point(  0.0, gH-1 );

		U.T.Transform( cnr );

		for( int k = 0; k < 4; ++k ) {

			B.L = fmin( B.L, cnr[k].x );
			B.R = fmax( B.R, cnr[k].x );
			B.B = fmin( B.B, cnr[k].y );
			B.T = fmax( B.T, cnr[k].y );
		}
	}
}

/* --------------------------------------------------------------- */
/* LayerAssignR -------------------------------------------------- */
/* --------------------------------------------------------------- */

void CTileSet::LayerAssignR( int is0, int isN, const DBox &B )
{
	Point	C( (B.L + B.R) / 2, (B.B + B.T) / 2 );

	for( int i = is0; i < isN; ++i ) {

		CUTile&	U = vtil[i];
		Point	cnr;	// (0,0)

		U.T.Transform( cnr );
		vaux[U.ix].r = cnr.DistSqr( C );
	}
}

/* --------------------------------------------------------------- */
/* ABOlap -------------------------------------------------------- */
/* --------------------------------------------------------------- */

// This is an ABOlap helper class for ordering vertices by angle

class OrdVert {

public:
	double	a;
	vertex	v;

public:
	OrdVert( const vertex& _v )
		{v = _v;};

	bool operator <  (const OrdVert &rhs) const
		{return a < rhs.a;};
};


// Return area(intersection) / (gW*gH).
//
// We construct the polygon enclosing the intersection in
// b-space. Its vertices comprise the set of rectangle edge
// crossings + any rectangle vertices that lie interior to
// the other.
//
// Once we've collected the vertices we order them by angle
// about their common centroid so they can be assembled into
// directed and ordered line segments for the area calculator.
//
double CTileSet::ABOlap( int a, int b )
{
	if( !vaux.size() )
		InitAuxData();

	vector<vertex>	va( 4 ), vb( 4 );

// Set b vertices (b-system) CCW ordered for LeftSide calcs

	vb[0] = vertex( 0   , 0 );
	vb[1] = vertex( gW-1, 0 );
	vb[2] = vertex( gW-1, gH-1 );
	vb[3] = vertex( 0   , gH-1 );

// Set a vertices (b-system) CCW ordered for LeftSide calcs

	{
		TForm			T;	// map a->b
		vector<Point>	p( 4 );

		MultiplyTrans( T, vaux[vtil[b].ix].inv, vtil[a].T );

		p[0] = Point( 0.0 , 0.0 );
		p[1] = Point( gW-1, 0.0 );
		p[2] = Point( gW-1, gH-1 );
		p[3] = Point( 0.0 , gH-1 );

		T.Transform( p );

		for( int i = 0; i < 4; ++i ) {

			va[i].x = int(p[i].x);
			va[i].y = int(p[i].y);

			// make close coords same

			for( int j = 0; j < i; ++j ) {

				if( iabs( va[i].x - va[j].x ) <= 2 )
					va[i].x = va[j].x;

				if( iabs( va[i].y - va[j].y ) <= 2 )
					va[i].y = va[j].y;
			}
		}
	}

// Gather all side intersections and all
// corners of A within B and vice versa.

	vector<OrdVert>	verts;

	for( int i = 0; i < 4; ++i ) {

		// count LeftSide successes for corner [i]

		int	ainb = 0,
			bina = 0;

		for( int j = 0; j < 4; ++j ) {

			// side crosses

			vertex	pi, pj;
			int		n;

			n = ClosedSegIsects( pi, pj,
					va[i], va[(i+1)%4],
					vb[j], vb[(j+1)%4] );

			switch( n ) {
				case 2:
					verts.push_back( OrdVert( pj ) );
				case 1:
					verts.push_back( OrdVert( pi ) );
				default:
					break;
			}

			// corner counts

			ainb += LeftSide( vb[j], vb[(j+1)%4], va[i] );
			bina += LeftSide( va[j], va[(j+1)%4], vb[i] );
		}

		if( ainb == 4 )
			verts.push_back( OrdVert( va[i] ) );

		if( bina == 4 )
			verts.push_back( OrdVert( vb[i] ) );
	}

// Intersection?

	int	nv = verts.size();

	if( nv < 3 )
		return 0.0;

// Centroid

	Point	C;

	for( int i = 0; i < nv; ++i ) {
		C.x += verts[i].v.x;
		C.y += verts[i].v.y;
	}

	C.x /= nv;
	C.y /= nv;

// Set OrdVert angles w.r.t centroid,
// and sort by angle.

	for( int i = 0; i < nv; ++i ) {

		OrdVert&	V = verts[i];

		V.a = atan2( V.v.y - C.y, V.v.x - C.x );
	}

	sort( verts.begin(), verts.end() );

// Remove any duplicate verts

	for( int i = nv - 1; i > 0; --i ) {

		for( int j = 0; j < i; ++j ) {

			if( verts[i].v == verts[j].v ||
				verts[i].a == verts[j].a ) {

				verts.erase( verts.begin() + i );
				--nv;
				break;
			}
		}
	}

// Assemble vertices into polygon

	vector<lineseg>	pgon;

	for( int i = 0; i < nv; ++i )
		pgon.push_back( lineseg( verts[i].v, verts[(i+1)%nv].v ) );

// Return area

	return AreaOfPolygon( pgon ) / (gW * gH);
}

