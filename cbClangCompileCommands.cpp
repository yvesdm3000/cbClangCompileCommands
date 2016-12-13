#include "cbClangCompileCommands.h"
#include <sdk.h> // Code::Blocks SDK

//#ifndef CB_PRECOMP
#include <manager.h>
#include <compilercommandgenerator.h>
#include <cbeditor.h>
#include <cbproject.h>
#include <compilerfactory.h>
#include <configmanager.h>
#include <editorcolourset.h>
#include <editormanager.h>
#include <logmanager.h>
#include <macrosmanager.h>
#include <projectfile.h>
#include <projectmanager.h>
#include <algorithm>
//#endif
#include <wx/tokenzr.h>
#include <wx/sstream.h>
#include <wx/mstream.h>
#include <wx/textfile.h>

//#include <configurationpanel.h>

static const wxString g_InvalidStr(wxT("invalid"));

// Register the plugin with Code::Blocks.
// We are using an anonymous namespace so we don't litter the global one.
namespace
{
    PluginRegistrant<cbClangCompileCommands> reg(_T("cbClangCompileCommands"));
}


// events handling
BEGIN_EVENT_TABLE(cbClangCompileCommands, cbPlugin)
    // add any events you want to handle here
END_EVENT_TABLE()

// constructor
cbClangCompileCommands::cbClangCompileCommands()
{
    // Make sure our resources are available.
    // In the generated boilerplate code we have no resources but when
    // we add some, it will be nice that this code is in place already ;)
    if(!Manager::LoadResource(_T("cbClangCompileCommands.zip")))
    {
        NotifyMissingFile(_T("cbClangCompileCommands.zip"));
    }
}

// destructor
cbClangCompileCommands::~cbClangCompileCommands()
{
}

void cbClangCompileCommands::OnAttach()
{
    typedef cbEventFunctor<cbClangCompileCommands, CodeBlocksEvent> CBEvent;
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_OPTIONS_CHANGED, new CBEvent(this, &cbClangCompileCommands::OnProjectOptionsChanged));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_FILE_ADDED, new CBEvent(this, &cbClangCompileCommands::OnProjectFileAdded));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_FILE_REMOVED, new CBEvent(this, &cbClangCompileCommands::OnProjectFileRemoved));
    Manager::Get()->RegisterEventSink(cbEVT_BUILDTARGET_SELECTED, new CBEvent(this, &cbClangCompileCommands::OnBuildTargetSelected));


}

void cbClangCompileCommands::OnRelease(bool appShutDown)
{
    Manager::Get()->RemoveAllEventSinksFor(this);
}

void cbClangCompileCommands::BuildMenu(wxMenuBar* menuBar)
{
}

void cbClangCompileCommands::BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data)
{
}

void cbClangCompileCommands::OnProjectOptionsChanged(CodeBlocksEvent& event)
{
    RebuildCompileCommands( event.GetProject() );
}
void cbClangCompileCommands::OnProjectFileAdded(CodeBlocksEvent& event)
{
    RebuildCompileCommands( event.GetProject() );
}
void cbClangCompileCommands::OnProjectFileRemoved(CodeBlocksEvent& event)
{
    RebuildCompileCommands( event.GetProject() );
}
void cbClangCompileCommands::OnBuildTargetSelected(CodeBlocksEvent& event)
{
    RebuildCompileCommands( event.GetProject() );
}

wxString cbClangCompileCommands::GetCompilerInclDirs(const wxString& compId)
{
    std::map<wxString, wxString>::const_iterator idItr = m_compInclDirs.find(compId);
    if (idItr != m_compInclDirs.end())
        return idItr->second;

    Compiler* comp = CompilerFactory::GetCompiler(compId);
    wxFileName fn(wxEmptyString, comp->GetPrograms().CPP);
    wxString masterPath = comp->GetMasterPath();
    Manager::Get()->GetMacrosManager()->ReplaceMacros(masterPath);
    fn.SetPath(masterPath);
    if (!fn.FileExists())
        fn.AppendDir(wxT("bin"));
#ifdef __WXMSW__
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ nul");
#else
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ /dev/null");
#endif // __WXMSW__
    wxArrayString output, errors;
    wxExecute(command, output, errors, wxEXEC_NODISABLE);

    wxArrayString::const_iterator errItr = errors.begin();
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("#include <...> search starts here:")))
        {
            ++errItr;
            break;
        }
    }
    wxString includeDirs;
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("End of search list.")))
            break;
        includeDirs += wxT(" -I\"") + errItr->Strip(wxString::both)+wxT("\"");
    }
    return m_compInclDirs.insert(std::pair<wxString, wxString>(compId, includeDirs)).first->second;
}


wxString cbClangCompileCommands::GetCompileCommand(ProjectFile* pf, const wxString& filename)
{
    wxString compileCommand;
    ProjectBuildTarget* target = nullptr;
    Compiler* comp = nullptr;

    if (!pf)
    {
        Manager::Get()->GetProjectManager()->FindProjectForFile( filename, &pf, false, false );
    }

    if (pf && pf->GetParentProject() && !pf->GetBuildTargets().IsEmpty())
    {
        target = pf->GetParentProject()->GetBuildTarget(pf->GetBuildTargets()[0]);
        comp = CompilerFactory::GetCompiler(target->GetCompilerID());
    }
    cbProject* proj = (pf ? pf->GetParentProject() : nullptr);
    if (!comp && proj)
        comp = CompilerFactory::GetCompiler(proj->GetCompilerID());
    if (!comp)
    {
        cbProject* tmpPrj = Manager::Get()->GetProjectManager()->GetActiveProject();
        if (tmpPrj)
            comp = CompilerFactory::GetCompiler(tmpPrj->GetCompilerID());
    }
    if (!comp)
        comp = CompilerFactory::GetDefaultCompiler();

    if (pf && (!pf->GetBuildTargets().IsEmpty()) )
    {
        target = pf->GetParentProject()->GetBuildTarget(pf->GetBuildTargets()[0]);

        if (pf->GetUseCustomBuildCommand(target->GetCompilerID() ))
            compileCommand = pf->GetCustomBuildCommand(target->GetCompilerID());

    }

    if (compileCommand.IsEmpty())
        compileCommand = /*comp->GetPrograms().CPP +*/ wxT("clang++ $options $includes");
    CompilerCommandGenerator* gen = comp->GetCommandGenerator(proj);
    if (gen)
        gen->GenerateCommandLine(compileCommand, target, pf, filename,
                                 g_InvalidStr, g_InvalidStr, g_InvalidStr );
    delete gen;


    std::vector<wxString> unknownOptions;
    unknownOptions.push_back(wxT("-Wno-unused-local-typedefs"));
    unknownOptions.push_back(wxT("-Wzero-as-null-pointer-constant"));
    unknownOptions.push_back(wxT("-mthreads"));
    std::sort(unknownOptions.begin(), unknownOptions.end());

    wxStringTokenizer tokenizer(compileCommand);
    compileCommand.Empty();
    wxString pathStr;
    while (tokenizer.HasMoreTokens())
    {
        wxString flag = tokenizer.GetNextToken();

        if (std::binary_search(unknownOptions.begin(), unknownOptions.end(), flag))
            continue;

        // make all include paths absolute, so clang does not choke if Code::Blocks switches directories
        if (flag.StartsWith(wxT("-I"), &pathStr))
        {
            if (pathStr.StartsWith(wxT("\""), NULL))
            {
                pathStr = pathStr.Mid(1);
            }
            if (pathStr.EndsWith(wxT("\""), NULL))
            {
                pathStr = pathStr.RemoveLast(1);
            }
            wxFileName path(pathStr);
            if (path.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE))
                flag = wxT("-I\"") + path.GetFullPath()+wxT("\"");
        }
        compileCommand += flag + wxT(" ");
    }
    compileCommand += GetCompilerInclDirs(comp->GetID());
    compileCommand += wxT("-c \"") + filename + wxT("\"");

    /*
    ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);

    if (cfg->ReadBool( _T("/cmdoption_wnoattributes") ))
        compileCommand += wxT(" -Wno-attributes ");
    else
        compileCommand += wxT(" -Wattributes ");

    if (cfg->ReadBool( _T("/cmdoption_wextratokens") ))
        compileCommand += wxT(" -Wextra-tokens ");
    else
        compileCommand += wxT(" -Wno-extra-tokens ");

    if (cfg->ReadBool( _T("/cmdoption_fparseallcomments") ))
        compileCommand += wxT(" -fparse-all-comments ");

    wxString extraOptions = cfg->Read(_T("/cmdoption_extra"));
    if (extraOptions.length() > 0)
    {
        extraOptions.Replace( wxT("\r"), wxT(" ") );
        extraOptions.Replace( wxT("\n"), wxT(" ") );
        extraOptions.Replace( wxT("\t"), wxT(" ") );
        compileCommand += wxT(" ") + extraOptions;
    }
    */

    return compileCommand;
}

void cbClangCompileCommands::WriteStringValue(wxFile& out, const wxString& value) const
{
    wxMemoryOutputStream os;

    os.PutC('\"');

    wxCharBuffer cb = value.ToUTF8();
    char* writeBuffer = cb.data();
    size_t len = strlen( writeBuffer );

    for (size_t i = 0; i < len; ++i)
    {
        char escapeChar = 0;
        switch(*writeBuffer)
        {
        case '\"':
            escapeChar = '\"';
            break;
        case '\\':
            escapeChar = '\\';
            break;
        case '\n':
            escapeChar = 'n';
            break;
        case '\r':
            escapeChar = 'r';
            break;
        case '\b':
            escapeChar = 'b';
            break;
        case '\t':
            escapeChar = 't';
            break;
        }
        if (escapeChar)
        {
            os.PutC('\\');
            os.PutC(escapeChar);
        }
        else
        {
            os.PutC(*writeBuffer);
        }
        writeBuffer++;
    }
    os.PutC( '\"' );

    wxStreamBuffer* osBuffer = os.GetOutputStreamBuffer();
    void* bufPtr = osBuffer->GetBufferStart();
    wxString str = wxString::FromUTF8( (const char*)bufPtr, os.GetLength() );
    out.Write( str );
}

void cbClangCompileCommands::RebuildCompileCommands(cbProject* pProj)
{
    wxFileName fn(pProj->GetCommonTopLevelPath(), wxT("compile_commands.json"));
    wxFile out(fn.GetFullPath(), wxFile::write);
    out.Write( wxT("[") );
    for (FilesList::iterator it = pProj->GetFilesList().begin(); it != pProj->GetFilesList().end(); ++it)
    {
        ProjectFile* f = *it;
        if (it == pProj->GetFilesList().begin())
        {
            out.Write(wxTextFile::GetEOL());
            out.Write(wxT("  "));
        }
        else
        {
            out.Write( wxT(",") );
            out.Write( wxTextFile::GetEOL() );
            out.Write( wxT("  ") );
        }
        out.Write( wxT("{ \"directory\": ") );
        //wxString executionDir = pProj->GetExecutionDir();
        wxString executionDir = f->file.GetPath();
#ifdef __WXMSW__
        while( executionDir.Replace( wxT("\\"), wxT("/") ))
            ;
#endif
        WriteStringValue( out, executionDir );
        out.Write( wxT(",") );
        out.Write( wxTextFile::GetEOL());
        out.Write( wxT("    \"command\": ") );
        wxString compileCommand = GetCompileCommand(f, f->file.GetFullName() );
        WriteStringValue( out, compileCommand );
        out.Write( wxT(",") );
        out.Write( wxTextFile::GetEOL() );
        out.Write( wxT("    \"file\": ") );
        WriteStringValue( out, f->file.GetFullName() );
        out.Write( wxT("  }") );
    }
    out.Write( wxTextFile::GetEOL() );
    out.Write( wxT("]") );
    out.Write( wxTextFile::GetEOL() );
    out.Close();
}


