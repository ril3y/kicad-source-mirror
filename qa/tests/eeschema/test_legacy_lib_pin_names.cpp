/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <qa_utils/wx_utils/unit_test_utils.h>

#include <lib_symbol.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_pin.h>
#include <wx/filename.h>


BOOST_AUTO_TEST_CASE( LegacyLibEmptyPinName )
{
    wxFileName libPath( KI_TEST::GetTestDataRootDir() );
    libPath.AppendDir( "eeschema" );
    libPath.AppendDir( "libs" );
    libPath.SetFullName( "4xxx.lib" );

    IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_LEGACY ) );
    LIB_SYMBOL*         symbol = pi->LoadSymbol( libPath.GetFullPath(), wxS( "4001" ) );

    BOOST_REQUIRE( symbol );

    std::vector<SCH_PIN*> vdd = symbol->GetPinsByNumber( wxS( "14" ) );
    std::vector<SCH_PIN*> in = symbol->GetPinsByNumber( wxS( "1" ) );

    BOOST_REQUIRE( !vdd.empty() );
    BOOST_REQUIRE( !in.empty() );
    BOOST_CHECK_EQUAL( vdd[0]->GetName(), wxS( "VDD" ) );

    for( SCH_PIN* pin : in )
        BOOST_CHECK( pin->GetName().IsEmpty() );

    for( SCH_PIN* pin : symbol->GetGraphicalPins() )
        BOOST_CHECK_NE( pin->GetName(), wxS( "~" ) );
}
