// This MFC Samples source code demonstrates using MFC Microsoft Office Fluent User Interface
// (the "Fluent UI") and is provided only as referential material to supplement the
// Microsoft Foundation Classes Reference and related electronic documentation
// included with the MFC C++ library software.
// License terms to copy, use or distribute the Fluent UI are available separately.
// To learn more about our Fluent UI licensing program, please visit
// https://go.microsoft.com/fwlink/?LinkId=238214.
//
// Copyright (C) Microsoft Corporation
// All rights reserved.


#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "IfcQueryMFC.h"
#endif

#define _USE_MATH_DEFINES

#include "IfcQueryDoc.h"

#include <propkey.h>

#include "window/MainFrame.h"
#include "IfcQueryDoc.h"
#include "BuildingUtils.h"
#include "SceneGraph/ConverterCarve2Coin3D.h"
#include "SceneGraph/SoPtr.h"
#include "SceneGraph/SceneGraphUtils.h"

#include <Inventor/nodes/SoFontStyle.h>
#include <Inventor/nodes/SoSelection.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoText2.h>

#include <ifcpp/geometry/Carve/GeometryConverter.h>
#include <ifcpp/reader/ReaderSTEP.h>
#include <ifcpp/reader/ReaderUtil.h>
#include <ifcpp/writer/WriterSTEP.h>
#include <ifcpp/writer/WriterUtil.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CIfcQueryDoc

IMPLEMENT_DYNCREATE( IfcQueryDoc, CDocument)

BEGIN_MESSAGE_MAP( IfcQueryDoc, CDocument)
	//ON_WM_CREATE()
	//ON_DOCUMENT_LOADING_DONE()
END_MESSAGE_MAP()



IfcQueryDoc::IfcQueryDoc()
{
	// TODO: add one-time construction code here
	//m_main_frame_ptr = nullptr;
}

IfcQueryDoc::~IfcQueryDoc(){}

BOOL IfcQueryDoc::OnNewDocument()
{
	//m_main_frame_ptr = nullptr;

	if( !CDocument::OnNewDocument() )
	{
		return FALSE;
	}

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)
	initSceneGraph();

	return TRUE;
}

SoSelection* selection = nullptr;
void made_selection( void * userdata, SoPath * path )
{
	(void)fprintf( stdout, "%sselected %s\n",
		userdata == (void *)1L ? "" : "de",
		path->getTail()->getTypeId().getName().getString() );

	selection->touch(); // to redraw
}

void IfcQueryDoc::initSceneGraph()
{
	if( !m_root_node.valid() )
	{
		m_root_node = new SoSeparator();
		m_root_node->setName( "root_node" );
		m_model_node = new SoSelection();
		m_model_node->setName( "model_node" );
		m_root_node->addChild( m_model_node.get() );

		m_model_node->policy = SoSelection::SHIFT;
		m_model_node->addSelectionCallback( made_selection, (void *)1L );
		m_model_node->addDeselectionCallback( made_selection, (void *)0L );
		selection = m_model_node.get();

		// coordinate axes
		if( true )
		{
			SceneGraphUtils::createCoordinateAxes( 52, m_root_node.get() );
		}

		SceneGraphUtils::createCoordinateGrid( m_root_node.get() );
	}

	if( !m_material_selection.valid() )
	{
		m_material_selection = new SoMaterial();
		m_material_selection->diffuseColor.setValue( 0.f, 1.f, 0.f );
	}
}

void IfcQueryDoc::AddView( CView* pView )
{

}

void IfcQueryDoc::loadFile( std::wstring& file_path )
{
	if( !m_root_node.valid() )
	{
		initSceneGraph();
	}
	if( !m_model_node.valid() )
	{
		initSceneGraph();
	}

	int	retval = PathFileExists( file_path.c_str() );
	if( retval != 1 )
	{
		cout << "\nThe file requested " << file_path.c_str() << " is not a valid file" << endl;
		return;
	}

	CMainFrame* pmain = (CMainFrame*)AfxGetMainWnd();

	m_model_node->removeAllChildren();

	m_reader_step = shared_ptr<ReaderSTEP>( new ReaderSTEP() );
	m_writer_step = shared_ptr<WriterSTEP>( new WriterSTEP() );
	m_ifc_model = shared_ptr<BuildingModel>( new BuildingModel() );
	m_geometry_converter = shared_ptr<GeometryConverter>( new GeometryConverter( m_ifc_model ) );
	size_t num_vert = m_geometry_converter->getGeomSettings()->getNumVerticesPerCircle();
	m_geometry_converter->getGeomSettings()->setNumVerticesPerCircle( 10 );

	size_t num_vert_arc = m_geometry_converter->getGeomSettings()->getMinNumVerticesPerArc();
	m_geometry_converter->getGeomSettings()->setMinNumVerticesPerArc( 3 );

	// set the message callbacks
	if( pmain )
	{
		if( pmain->m_message_target )
		{
			m_doc_message_target = pmain->m_message_target;
			m_reader_step->setMessageTarget( pmain->m_message_target.get() );
			m_writer_step->setMessageTarget( pmain->m_message_target.get() );
			m_geometry_converter->setMessageTarget( pmain->m_message_target.get() );
		}
	}
	shared_ptr<GeometrySettings>& geom_settings = m_geometry_converter->getGeomSettings();
	m_converter_coin = std::make_shared<ConverterCarve2Coin3D>( geom_settings );

	std::stringstream err;

	try
	{
		m_reader_step->loadModelFromFile( file_path, m_ifc_model );

		// convert IFC geometry to Carve, perform boolean operations
		m_geometry_converter->convertGeometry();

		// convert Carve geometry to Coin3d
		m_converter_coin->setMessageTarget( m_geometry_converter.get() );
		m_converter_coin->convertToCoin3D( m_geometry_converter->getShapeInputData(), m_model_node.get() );

		// in case there are IFC entities that are not in the spatial structure
		const std::map<int, shared_ptr<BuildingObject> >& objects_outside_spatial_structure = m_geometry_converter->getObjectsOutsideSpatialStructure();
		if( objects_outside_spatial_structure.size() > 0 )
		{
			SoSeparator* sw_objects_outside_spatial_structure = new SoSeparator();
			sw_objects_outside_spatial_structure->setName( "IfcProduct-objects-outside-spatial-structure" );

			m_converter_coin->addNodes( objects_outside_spatial_structure, sw_objects_outside_spatial_structure );
			if( sw_objects_outside_spatial_structure->getNumChildren() > 0 )
			{
				m_model_node->addChild( sw_objects_outside_spatial_structure );
			}
		}
	}
	catch( BuildingException& e )
	{
		err << e.what();
	}
	catch( std::exception& e )
	{
		err << e.what();
	}

	if( pmain )
	{
		// set progress to 100
		shared_ptr<StatusCallback::Message> message_progress( new StatusCallback::Message() );
		message_progress->m_message_type = StatusCallback::MESSAGE_TYPE_PROGRESS_VALUE;
		message_progress->m_progress_value = 100;
		OnStatusMessage( message_progress );

		// reset progress to 0
		message_progress->m_progress_value = 0;
		OnStatusMessage( message_progress );

		pmain->OnUpdate();
		pmain->zoomToModel();
	}
}

void IfcQueryDoc::unselectAllNodes()
{
	for( auto it = m_vec_selected_nodes.begin(); it != m_vec_selected_nodes.end(); ++it )
	{
		shared_ptr<SceneGraphUtils::SelectionContainer>& selected_node_container = (*it);
		SceneGraphUtils::unselectNodeContainer( selected_node_container );
	}
	m_vec_selected_nodes.clear();
}

BOOL IfcQueryDoc::OnOpenDocument( LPCTSTR lpszPathName )
{
	if( !CDocument::OnOpenDocument( lpszPathName ) )
	{
		return FALSE;
	}

	if( lstrlen( lpszPathName ) == 0 )
	{
		return FALSE;
	}

	std::wstring file_path = lpszPathName;
	loadFile( file_path );

	return TRUE;
}

void IfcQueryDoc::OnStatusMessage( const shared_ptr<StatusCallback::Message>& m )
{
	if( m_doc_message_target )
	{
		m_doc_message_target->messageCallback( m );
	}
}

// CIfcQueryDoc serialization
void IfcQueryDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

#ifdef SHARED_HANDLERS

// Support for thumbnails
void IfcQueryDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// Modify this code to draw the document's data
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// Support for Search Handlers
void IfcQueryDoc::InitializeSearchContent()
{
	CString strSearchContent;
	// Set search contents from document's data.
	// The content parts should be separated by ";"

	// For example:  strSearchContent = _T("point;rectangle;circle;ole object;");
	SetSearchContent(strSearchContent);
}

void IfcQueryDoc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl *pChunk = nullptr;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != nullptr)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CIfcQueryDoc diagnostics

#ifdef _DEBUG
void IfcQueryDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void IfcQueryDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CIfcQueryDoc commands
