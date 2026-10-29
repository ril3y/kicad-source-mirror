/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "eeschema_helpers.h"

#include <connection_graph.h>
#include <locale_io.h>
#include <schematic.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_label.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_screen.h>
#include <settings/settings_manager.h>
#include <wildcards_and_files_ext.h>
#include <tool/tool_manager.h>
#include <kiface_base.h>

#include <wx/app.h>


SCH_EDIT_FRAME*   EESCHEMA_HELPERS::s_SchEditFrame = nullptr;


void EESCHEMA_HELPERS::SetSchEditFrame( SCH_EDIT_FRAME* aSchEditFrame )
{
    s_SchEditFrame = aSchEditFrame;
}


SCHEMATIC* EESCHEMA_HELPERS::LoadSchematic( const wxString& aFileName, bool aSetActive,
                                            bool aForceDefaultProject, PROJECT* aProject, bool aCalculateConnectivity )
{
    if( aFileName.EndsWith( FILEEXT::KiCadSchematicFileExtension ) )
        return LoadSchematic( aFileName, SCH_IO_MGR::SCH_KICAD, aSetActive, aForceDefaultProject,
                              aProject, aCalculateConnectivity );
    else if( aFileName.EndsWith( FILEEXT::LegacySchematicFileExtension ) )
        return LoadSchematic( aFileName, SCH_IO_MGR::SCH_LEGACY, aSetActive, aForceDefaultProject,
                              aProject, aCalculateConnectivity );

    // as fall back for any other kind use the legacy format
    return LoadSchematic( aFileName, SCH_IO_MGR::SCH_LEGACY, aSetActive, aForceDefaultProject, aProject,
                          aCalculateConnectivity );
}


SCHEMATIC* EESCHEMA_HELPERS::LoadSchematic( const wxString& aFileName,
                                            SCH_IO_MGR::SCH_FILE_T aFormat,
                                            bool aSetActive,
                                            bool aForceDefaultProject,
                                            PROJECT* aProject,
                                            bool aCalculateConnectivity )
{
    wxFileName pro = aFileName;
    pro.SetExt( FILEEXT::ProjectFileExtension );
    pro.MakeAbsolute();
    wxString projectPath = pro.GetFullPath();

    // Ensure the "C" locale is temporary set, before reading any file
    // It also avoid wxWidget alerts about locale issues, later, when using Python 3
    LOCALE_IO dummy;

    PROJECT* project = aProject;
    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();

    if( !project )
    {
        project = mgr.GetProject( projectPath );
    }

    if( !aForceDefaultProject )
    {
        if( !project )
        {
            // A legacy .pro project is migrated to the current format when loaded
            wxFileName legacyPro = pro;
            legacyPro.SetExt( FILEEXT::LegacyProjectFileExtension );

            if( wxFileExists( projectPath ) || legacyPro.FileExists() )
            {
                mgr.LoadProject( projectPath, aSetActive );
                project = mgr.GetProject( projectPath );
            }
        }
        else if( s_SchEditFrame && project == &mgr.Prj() )
        {
            // Project is already loaded?  Then so is the board
            return &s_SchEditFrame->Schematic();
        }
    }

    // Board cannot be loaded without a project, so create the default project
    if( !project || aForceDefaultProject )
        project = &mgr.Prj();

    IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( aFormat ) );

    SCHEMATIC* schematic = new SCHEMATIC( project );
    schematic->CreateDefaultScreens();

    wxFileName schFile = aFileName;
    schFile.MakeAbsolute();

    try
    {
        SCH_SHEET* rootSheet = pi->LoadSchematicFile( schFile.GetFullPath(), schematic );

        if( rootSheet )
            schematic->SetTopLevelSheets( { rootSheet } );
        else
            return nullptr;
    }
    catch( ... )
    {
        return nullptr;
    }

    if( aFormat == SCH_IO_MGR::SCH_LEGACY )
        MigrateLegacySchematic( schematic );

    SCH_SHEET_LIST sheetList = schematic->BuildSheetListSortedByPageNumbers();
    SCH_SCREENS    screens( schematic->Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        screen->UpdateLocalLibSymbolLinks();

    if( schematic->RootScreen()->GetFileFormatVersionAtLoad() < 20221002 )
        sheetList.UpdateSymbolInstanceData( schematic->RootScreen()->GetSymbolInstances());

    sheetList.UpdateSheetInstanceData( schematic->RootScreen()->GetSheetInstances());

    if( aFormat == SCH_IO_MGR::SCH_LEGACY
        || schematic->RootScreen()->GetFileFormatVersionAtLoad() < 20230221 )
    {
        screens.FixLegacyPowerSymbolMismatches();
    }

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        screen->MigrateSimModels();

    if( aFormat == SCH_IO_MGR::SCH_LEGACY )
    {
        // Legacy files store the reference and unit on the symbol itself; create the
        // per-sheet-path instances that hold them in the current format.
        screens.PruneOrphanedSymbolInstances( project->GetProjectName(), sheetList );
        screens.PruneOrphanedSheetInstances( project->GetProjectName(), sheetList );
        sheetList.CheckForMissingSymbolInstances( project->GetProjectName() );
    }

    sheetList.AnnotatePowerSymbols();

    schematic->ConnectionGraph()->Reset();

    TOOL_MANAGER* toolManager = new TOOL_MANAGER;
    toolManager->SetEnvironment( schematic, nullptr, nullptr, Kiface().KifaceSettings(), nullptr );

    if( aCalculateConnectivity )
    {
        SCH_COMMIT dummyCommit( toolManager );
        schematic->RecalculateConnections( &dummyCommit, GLOBAL_CLEANUP, toolManager );
    }

    // Same as the schematic editor: merging colinear wire segments can break connectivity in
    // legacy files saved without valid libraries unless the implied junctions are re-created.
    if( aFormat == SCH_IO_MGR::SCH_LEGACY )
        schematic->FixupJunctionsAfterImport();

    schematic->ResolveERCExclusionsPostUpdate();

    schematic->SetSheetNumberAndCount();
    schematic->RecomputeIntersheetRefs();

    for( SCH_SHEET_PATH& sheet : sheetList )
    {
        sheet.UpdateAllScreenReferences();
        sheet.LastScreen()->TestDanglingEnds( nullptr, nullptr );
    }

    if( aCalculateConnectivity )
        schematic->ConnectionGraph()->Recalculate( sheetList, true );

    return schematic;
}


void EESCHEMA_HELPERS::MigrateLegacySchematic( SCHEMATIC* aSchematic )
{
    SCH_SCREENS screens( aSchematic->Root() );

    // Same as the s-expression parser: a top-level sheet takes the UUID of its schematic file,
    // which is what symbol instance paths start with once the file is saved and reloaded.
    for( SCH_SHEET* sheet : aSchematic->GetTopLevelSheets() )
    {
        if( sheet->GetScreen() )
            const_cast<KIID&>( sheet->m_Uuid ) = sheet->GetScreen()->GetUuid();
    }

    screens.ConvertBusBusEntries();

    // Link symbols through the project symbol library table, falling back to the legacy
    // cache library for symbols whose library is no longer available.
    screens.UpdateSymbolLinks();

    // Legacy schematics can have duplicate time stamps so fix that before converting
    // to the s-expression format.
    screens.ReplaceDuplicateTimeStamps();
}
