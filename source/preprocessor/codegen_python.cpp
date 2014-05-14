/******************************************************************************
 *
 * MantaFlow fluid solver framework
 * Copyright 2011-2014 Tobias Pfaff, Nils Thuerey 
 *
 * This program is free software, distributed under the terms of the
 * GNU General Public License (GPL) 
 * http://www.gnu.org/licenses
 *
 * Preprocessor: Process replacement text of PYTHON keywords
 *
 ******************************************************************************/

#include "prep.h"
#include <cstdlib>
#include <set>
#include <sstream>
#include <iostream>
using namespace std;


enum FunctionType { FtPlugin, FtMember, FtConstructor };
void createPythonWrapper(const List<Argument>& args, const string& fname, const string& totalname, FunctionType ftype, string& header, string& footer, string& callList, bool isClass, bool hasParent) {
    // beautify code for debug mode
    string nl  = (gDebugMode && ftype != FtConstructor) ? "\n" : " ";
    string tb  = (gDebugMode && ftype != FtConstructor) ? (isClass ? "\t\t" : "\t") : "";
    string tb1 = (gDebugMode && ftype != FtConstructor) ? (isClass ? "\t" : "") : "";
    string tb2 = tb1+tb;

    const string argstr = (ftype == FtMember) ? "__args" : "_args";
    // load arguments from python
    callList = "";
    string loader = "", argList = "";
    for (size_t i=0; i<args.size(); i++) {
        Type type = args[i].type;        
        bool itype = isIntegral(type.name);
        string name = args[i].name, typeName = type.name;
        stringstream num; num << args[i].index;
        if (!type.templateTypes.empty()) typeName += type.templateTypes.minimal;
        if (type.isPointer) typeName += "*";        
        string completeType = (type.isConst) ? ("const " + typeName) : typeName;
                
        if (type.isRef) {
            if (args[i].value.empty()) {                
                // for strings etc. direct get, for PbClass use pointers
                if (itype)
                    loader += tb2+ completeType + "& " + name + " = " + argstr + ".get< " + typeName + " > (" + num.str() + ",\"" + name + "\", &_lock);" + nl;
                else
                    loader += tb2+ completeType + "& " + name + " = *" + argstr + ".get< " + typeName + "* > (" + num.str() + ",\"" + name + "\", &_lock);" + nl;
            } else {                
                // for strings etc. direct get, for PbClass use pointers
                if (itype) {
                    loader += tb2+ completeType + "& " + name + " = " + argstr + ".getOpt< " + typeName + " > (" + num.str() + ",\"" + name + "\", " + args[i].value + ", &_lock);" + nl;
                } else {
                    loader += tb2+ completeType + "* _ptr_" + name + " = " + argstr + ".getOpt< " + typeName + "* > (" + num.str() + ",\"" + name + "\", 0, &_lock);" + nl;
                    loader += tb2+ completeType + "& " + name + " = (_ptr_" + name + ") ? (*_ptr_" + name + ") : (" + args[i].value + ");" + nl; 
                }
            }
            typeName += "&";
        }
        else {
            loader += tb2+ completeType + " " + name + " = ";
            if (args[i].value.empty()) {
                loader += argstr + ".get< " + typeName + " > (" + num.str() + ",\"" + name + "\", &_lock);" + nl;
            } else {
                loader += argstr + ".getOpt< " + typeName + " > (" + num.str() + ",\"" + name + "\", " + args[i].value + ", &_lock);" + nl;
            }
        }
        if (i!=0) callList +=", ";
        callList += name;
    }
    
    // generate header & footer
    if (ftype == FtConstructor) {
        header = "int " + fname + " (PyObject* _self, PyObject* _linargs, PyObject* _kwds) {" + nl;
        header += tb2+ "PbClass* obj = PbClass::fromPyObject(_self);" + nl;
        header += tb2+ "if (obj) delete obj;" + nl;                
    } else
        header = "PyObject* " + fname + " (PyObject* _self, PyObject* _linargs, PyObject* _kwds) {" + nl;
    header += tb+"try {" + nl;
    header += tb2+ "PbArgs " + argstr + "(_linargs, _kwds);" + nl;
    if (ftype == FtMember)
        header += tb2+ "PyObject *_retval = NULL;" + nl;
    if (ftype == FtPlugin) {
		if(hasParent) header += tb2+ "FluidSolver *parent = _args.obtainParent();" + nl;
		else          header += tb2+ "FluidSolver *parent = NULL;" + nl;
        header += tb2+ "pbPreparePlugin(parent, \""+totalname+"\" );" + nl;
        header += tb2+ "PyObject *_retval = NULL;" + nl;
    } else if (ftype == FtMember)
        header += tb2+ "pbPreparePlugin(this->mParent, \""+totalname+"\");" + nl;        
    else 
        header += tb2+ "pbPreparePlugin(0, \""+totalname+"\");" + nl;    
    header += tb2+ "{ ArgLocker _lock;" + nl;
    header += loader;
    if (ftype == FtMember)
        header += tb2+ "this->_args.copy(__args);" + nl;
            
    if (ftype == FtConstructor) {
        footer =  tb2+ "std::string _name = _args.getOpt<std::string>(\"name\",\"\");" + nl;
        footer += tb2+ "obj->setPyObject(_self);" + nl;
        footer += tb2+ "if (!_name.empty()) obj->setName(_name);" + nl;
        footer += tb2+ "_args.check(); }" + nl;
        footer += tb2+ "pbFinalizePlugin(obj->getParent(),\"" +totalname+"\");" + nl;
        footer += tb2+ "return 0;" + nl;
    } else if (ftype == FtMember) {
        footer =  tb2+ "this->_args.check(); }" + nl;
        footer += tb2+ "pbFinalizePlugin(this->mParent,\"" +totalname+"\");" + nl;
        footer += tb2+ "return _retval;" + nl;    
    } else if (ftype == FtPlugin) {
        footer =  tb2+ "_args.check(); }" + nl;
        footer += tb2+ "pbFinalizePlugin(parent,\"" +totalname+"\" );" + nl;
        footer += tb2+ "return (_retval) ? _retval : getPyNone();" + nl;    
    }
    footer += tb+ "} catch(std::exception& e) {" + nl;
    footer += tb2+ "pbSetError(\"" + totalname + "\",e.what());" + nl;
    footer += tb2+ "return " + ((ftype==FtConstructor) ? "-1;" : "0;") + nl;
    footer += tb+ "}" + nl;
    footer += tb1+ "}" + nl;
}

string createConverters(const string& name, const string& tb, const string& nl, const string& nlr) {
    return "template<> " + name + "* fromPy<" + name + "*>(PyObject* obj) {" + nl +
           tb+ "if (PbClass::isNullRef(obj)) return 0;" + nl +
           tb+ "PbClass* pbo = PbClass::fromPyObject(obj);" + nl +
           tb+ "if (!pbo || !(pbo->canConvertTo(\"" + name + "\")))" + nl +
           tb+tb+ "throw Error(\"can't convert argument to type '" + name + "'\");" + nl +
           tb+ "return dynamic_cast<" + name + "*>(pbo);" + nl +
           "}" + nlr +
           "template<> PyObject* toPy< " + name + " >( " + name + "& v) {" + nl +
           tb+ "if (v.getPyObject()) return v.getPyObject();" + nl +
           tb+ name + "* co = new " + name + " (v); " +
           tb+ "return co->assignNewPyObject(\""+name+"\");" + nl +
           "}" + nlr;
}

// globals for tracking state between python class and python function registrations
string gLocalReg, gParent;
bool gFoundConstructor = false, gIsTemplated=false;

string processPythonFunction(const Block& block, const string& code) {
    const string name = block.func.name;
    const List<Argument> args = block.func.arguments;
    const Type type = block.func.returnType;
    
    // beautify code
    string nl = gDebugMode ? "\n" : "";
    string tb = (gDebugMode) ? "\t" : "";
    
    string inlineS = block.func.isInline ? "inline " : "";
    if (block.func.isVirtual) inlineS += "virtual ";
    const string constS = block.func.isConst ? " const" : "";

	// PYTHON(...) keyword options
	bool hasParent = true;
    for (size_t i=0; i<block.options.size(); i++) {
        if (block.options[i].name == "noparent") {
            hasParent = false;
		}
	}
    
    // is header file ?
    bool isHeader = gFilename[gFilename.size()-2] == '.' && gFilename[gFilename.size()-1] == 'h';
    bool isConstructor = type.minimal.empty();
    bool isPlugin = gParent.empty();
    if (isConstructor) gFoundConstructor = true;
        
    // generate caller
    string clname = "_" + gParent + (gIsTemplated ? "@" : "");
    string fname = "_" + name;
    if (isPlugin) {
        clname = ""; fname = "_plugin_" + name;
    }
    string codeInline = code;
    if (code[0] != ';') {
        codeInline = "{" + nl;
        codeInline += code.substr(1,code.size()-1) + nl;        
    }
        
    // document free plugins
    if (isPlugin && gDocMode) {
        string ds = "//! \\ingroup Plugins\nPYTHON " + fname + "( ";
        for(size_t i=0; i<args.size(); i++) {  if (i!=0) ds+=", "; ds+=args[i].minimal;}
        return ds + " ) {}\n";
    }
    
    string header, footer, callList;
    FunctionType funcType = isConstructor ? FtConstructor : FtMember;
    if (isPlugin) funcType = FtPlugin;
    const string displayName = gParent.empty() ? name : (gParent+"::"+name);
    createPythonWrapper(args, isConstructor ? (clname+clname) : fname, displayName, funcType, header, footer, callList, !isPlugin, hasParent); 

    string caller = (isPlugin ? "" : tb ) + header + nl;
    if (isPlugin) {
        if (!callList.empty()) callList +=", ";
        callList += "parent";
    }
    if (type.name == "void") {
        caller += tb+tb+tb+ "_retval = getPyNone();" + nl;
        caller += tb+tb+tb+ name + "(" + callList + ");" + nl + nl;
    } else
        caller += tb+tb+tb+ "_retval = d_toPy(" + name + "(" + callList + ") );" + nl + nl;
    caller += footer;
    
    // replicate original function
    string func = inlineS + type.minimal + " " + name + "(" + args.listText;
    if (isPlugin) {
        // add parent as argument
        if (args.size()>0) func += ",";
        func += " FluidSolver* parent = NULL, PbArgs& _args = PbArgs::EMPTY";
    }
    func += ") " + constS + block.initList + codeInline + nl;
        
    // register
    string regname = clname + (isConstructor ? clname : fname);
    string regHeader = (isConstructor ? "int " : "PyObject* ") + regname + " (PyObject* _self, PyObject* _linargs, PyObject* _kwds)";
    string regDecl = "", regCall = "";
    if (isConstructor) {
        caller = "";
        if (gIsTemplated) {
            regDecl = "@template " + gParent + " " + header + "obj = new " + gParent + "<$$> (" + callList + ");" + footer;
            regCall = "@template " + gParent + " PbWrapperRegistry::instance().addConstructor(\"" + gParent + "<$$>\", " + regname + ");";        
        } else {
            regDecl = header + "obj = new " + gParent + "(" + callList + ");" + footer;
            regCall = "PbWrapperRegistry::instance().addConstructor(\"" + gParent + "\", " + regname + ");";
        }
    } else {
        if (gIsTemplated) {
            regDecl = "@template " + gParent + " " + regHeader + " { return dynamic_cast<" + gParent +"<$$>*>(PbClass::fromPyObject(_self))->_" + name + "(_self, _linargs, _kwds); }";
            regCall = "@template " + gParent + " PbWrapperRegistry::instance().addMethod(\"" + gParent + "<$$>\", \"" + name + "\", " + regname + ");";
        } else {
            regDecl = regHeader + " { return fromPy<" + gParent +"*>(_self)->_" + name + "(_self, _linargs, _kwds); }";
            regCall = "PbWrapperRegistry::instance().addMethod(\"" + gParent + "\", \"" + name + "\", " + regname + ");";
        }
    }
    if (isHeader) {
        gRegText += regDecl + "\n" + regCall + "\n";
    } else {
        gLocalReg += regDecl + nl;
        gRegText += "extern " + regHeader + ";\n" + regCall + "\n";
    }
    
    if (gDocMode) {
        caller = "";
        func = "PYTHON " + func;
    }    
    return block.linebreaks() + func + nl + caller;
}

string processPythonVariable(const Block& block) {
    // beautify code
    string nl = gDebugMode ? "\n" : "";
    string tb = gDebugMode ? "\t" : "";
    const int line = block.line0;
    
    // is header file ?
    bool isHeader = gFilename[gFilename.size()-2] == '.' && gFilename[gFilename.size()-1] == 'h';
    
    if (gParent.empty())
        errMsg(line, "PYTHON variables con only be used inside classes");
    
    string name=block.func.name;
    string pname=name, type = block.func.returnType.name;
    
    // process options
    for (size_t i=0; i<block.options.size(); i++) {
        if (block.options[i].name == "name") 
            pname = block.options[i].value;
        else
            errMsg(line, "PYTHON(opt): illegal option. Supported options are: 'name'");
    }
    
    // define getter / setter
    string nn = gParent + "_" +name;
    string gethdr = "PyObject* _get_" + nn + "(PyObject* self, void* cl)";
    string sethdr = "int _set_" + nn + "(PyObject* self, PyObject* val, void* cl)";
    
    // replicate original code plus accessor
    string code = "";
    code += type + " " + name + ";" + nl;
    code += tb + "friend " + gethdr + ";" + nl;
    code += tb + "friend " + sethdr + ";" + nl;
    
    // add get/setter
    string getter = gethdr+" { return d_toPy(fromPy<" + gParent+"*>(self)->" + name + "); }";
    string setter = sethdr+" { fromPy<" + gParent+"*>(self)->" + name + "=fromPy<" + type + " >(val); return 0;}";
        
    // register
    gRegText += "PbWrapperRegistry::instance().addGetSet(\""+gParent+"\",\""+pname+"\",_get_"+nn+ ",_set_"+nn+");\n";
    if (isHeader) {
        gRegText += getter+"\n"+setter+"\n";
    } else {
        gLocalReg += getter + nl + setter + nl;
        gRegText += "extern " + gethdr + ";\nextern " + sethdr + ";\n";
    }
    
    return block.linebreaks() + code;
}

string processPythonClass(const Block& block, const string& code) {
    // beautify code
    string nl = gDebugMode ? "\n" : "";
    string tb = gDebugMode ? "\t" : "";
    const List<Type>& templArgs = block.cls.templateTypes;
    const int line = block.line0;

    // is header file ?
    bool isHeader = gFilename[gFilename.size()-2] == '.' && gFilename[gFilename.size()-1] == 'h';
    
    if (!isHeader && !templArgs.empty())
        errMsg(line, "PYTHON template classes can only be defined in header files.");
    
    string name = block.cls.name;
    string pname = name; // for now
    string baseclassName = block.cls.baseClass.name;
    const List<Type>& baseclassTempl = block.cls.baseClass.templateTypes;
    
    // process options
    for (size_t i=0; i<block.options.size(); i++) {
        if (block.options[i].name == "name") 
            pname = block.options[i].value;
        else
            errMsg(line, "PYTHON(opt): illegal kernel option. Supported options are: 'name'");
    }
    
    // class registry
    string baseclass = baseclassName, modBase = baseclassName, registry = "", implInst = "";
    if (!baseclassTempl.empty()) {
        // baseclass known ? try to implicitly instantiate base class
        string targ="", tcarg="" ,bclist="";
        bool chain=false;
        for (size_t i=0; i<baseclassTempl.size(); i++) {
            // check if template arg            
            int index = -1;
            for (size_t j=0; j<templArgs.size(); j++) {
                if (templArgs[j].name == baseclassTempl[i].name) {
                    index = j;
                    chain=true;
                    break;
                }
            }
            if (index>=0) {
                targ += "@"+baseclassTempl[i].name+"@";
                stringstream s;
                s << "$" << index << "$";
                bclist += s.str();
            } else {
                targ += baseclassTempl[i].name;
                bclist += baseclassTempl[i].name;
            }
            if (i!=baseclassTempl.size()-1) { targ += ","; bclist += ","; }
        }
        for (size_t i=0; i<templArgs.size(); i++) {
            tcarg += "@" + templArgs[i].name + "@";
            if (i!=templArgs.size()-1) tcarg += ",";
        }
        string aliasn = "_" + baseclassName + "_" + targ;
        replaceAll(aliasn,",","_");
        
        // need defer chain ?
        if (chain){
            gRegText += "@chain " + name + " " + tcarg + " " + baseclassName + " " + targ + "\n";
        } else {
            gRegText += "@instance " + baseclassName + " " + targ + " " + aliasn + "\n";    
        }
        modBase += "<" + bclist + ">";
        baseclass += baseclassTempl.minimal;
    }
    if (templArgs.empty())
        registry = "PbWrapperRegistry::instance().addClass(\"" + pname + "\", \"" + name + "\", \"" + modBase + "\");";
    else {
        registry = "@template " + name + " PbWrapperRegistry::instance().addClass(\"@\", \"" + name + "<$$>\", \"" + modBase + "\");";
    }
    
    // register class
    if (isHeader) {
        // make sure we
        string fn = gFilename;
        const size_t p = fn.find_last_of('/');
        if (p != string::npos) fn=fn.substr(p+1);
        gRegText += "#include \"" + fn + "\"\n";        
    }
    gRegText += registry + "\n";
    
    // register converters
    gLocalReg = ""; 
    if (templArgs.empty()) {
        if (isHeader)
            gRegText += createConverters(name, "", " ", "\n");
        else
            gLocalReg += createConverters(name, tb, nl, nl);
    }
    
    // tokenize and parse contained python functions
    gParent = name;
    gIsTemplated = !templArgs.empty();
    gFoundConstructor = false;
    string newText = processText(code.substr(1), line);
    gParent = "";    
    if (!gFoundConstructor)
        errMsg(line, "no PYTHON constructor found in class '" + name + "'");
    if (!isHeader && gIsTemplated)
        errMsg(line, "PYTHON class template can only be used in header files.");
    
    // create class
    string pclass = "";
    if (gDocMode) {
        pclass += "//! \\ingroup PyClasses\nPYTHON ";
    }
    if (gIsTemplated)
        pclass += "template" + templArgs.minimal + nl;
    pclass += "class " + name + " : public " + baseclass + " {" + nl;
    pclass += newText + nl;
    if (!gDocMode) {
        pclass += "public:" + nl;
        pclass += tb+"PbArgs _args;" + nl;
    }
    pclass += "};" + nl;
    
    return block.linebreaks() + implInst + pclass + gLocalReg;
}

set<string> gAliasRegister;
string processPythonInstantiation(const Block& block, const Type& aliasType, const string& aliasName) {
    gRegText += "@instance " + aliasType.name + " " + aliasType.templateTypes.listText + " " + aliasName + "\n";
    
    if (gAliasRegister.find(aliasName) == gAliasRegister.end()) {
        gAliasRegister.insert(aliasName);
        return block.linebreaks() + "typedef " + aliasType.minimal + " " + aliasName + "; ";
    }
    return block.linebreaks();
}
