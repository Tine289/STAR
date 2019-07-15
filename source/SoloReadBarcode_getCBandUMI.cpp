#include "SoloReadBarcode.h"
#include "serviceFuns.cpp"
#include "SequenceFuns.h"

void SoloReadBarcode::matchCBtoWL(string &cbSeq1, string &cbQual1, vector<uint64> &cbWL, int32 &cbMatch1, vector<uint64> &cbMatchInd1, string &cbMatchString1)
{
    cbMatch1=-1;
    cbMatchString1="";
    cbMatchInd1.clear();
    //convert CB and check for Ns
    uint64 cbB1;
    int64 posN=convertNuclStrToInt64(cbSeq1,cbB1);

    if (!pSolo.cbWLyes) {//no whitelist - no search
        if (posN!=-1) {//Ns are present, discard this read
            stats.V[stats.nNinBarcode]++;
        } else {//no Ns
            //cbI=(int64) cbB1;
            cbMatchInd1.push_back(cbB1);//all possible barcodes are accepted. This will overflow if CB is longer than 31b
            cbMatchString1 = to_string(cbB1);
            cbMatch1=0;
        };
        return;
    };

    if (posN==-2) {//>2 Ns, might already be filtered by Illumina
        stats.V[stats.nNinBarcode]++;
        return;
    } else if (posN==-1) {//no Ns, count only for featureType==gene
        int64 cbI=binarySearchExact<uint64>(cbB1,cbWL.data(),cbWL.size());
        if (cbI>=0) {//exact match
            cbMatchInd1.push_back((uint64) cbI);
            cbMatchString1 = to_string(cbMatchInd1[0]);
            cbMatch1=0;
            return;
        };
    };
    
    if (pSolo.CBmatchWLtype==0) //only exact matches allowed
        return;

    if (posN>=0) {//one N
        int64 cbI=-1;
        uint32 posNshift=2*(cbSeq1.size()-1-posN);//shift bits for posN
        for (uint32 jj=0; jj<4; jj++) {
            uint64 cbB11=cbB1^(jj<<posNshift);
            int64 cbI1=binarySearchExact<uint64>(cbB11,cbWL.data(),cbWL.size());
            if (cbI1>=0) {
                if (cbI>=0) {//had another match already
                    stats.V[stats.nTooMany]++;
                    return;//with N in CB, do not allow matching >1 in WL
                };
                cbI=cbI1;
            };
        };
        if (cbI>=0) {
            cbMatchInd1.push_back((uint64) cbI);
            cbMatchString1 = to_string(cbMatchInd1[0]);
            cbMatch1=1;
            return;
        } else {//no match
            stats.V[stats.nNoMatch]++;
            return;
        };
    };

    //look for 1MM; posN==-1, no Ns
    cbMatch1=0;
    for (uint32 ii=0; ii<cbSeq1.size(); ii++) {
        for (uint32 jj=1; jj<4; jj++) {
            int64 cbI1=binarySearchExact<uint64>(cbB1^(jj<<(ii*2)),cbWL.data(),cbWL.size());
            if (cbI1>=0) {//found match
                //output all
                cbMatchInd1.push_back(cbI1);
                ++cbMatch1;
                cbMatchString1 += ' ' +to_string(cbI1) + ' ' + cbQual1.at(cbSeq1.size()-1-ii);
            };
        };
    };
    if (cbMatch1==0) {//no matches
        stats.V[stats.nNoMatch]++;
        cbMatch1=-1;
    } else if (cbMatch1==1) {//1 match, no need to record the quality
        cbMatchString1 = to_string(cbMatchInd1[0]);
    } else if (pSolo.CBmatchWLtype==1) {//>1 matches, but this is not allowed
        stats.V[stats.nTooMany]++;
        cbMatch1=-1;
        cbMatchInd1.clear();
        cbMatchString1="";
    };// else cbMatch contains number of matches, and cbMatchString has CBs and qualities
};

bool SoloReadBarcode::convertCheckUMI()
{//check UMIs, return if bad UMIs
    if (convertNuclStrToInt32(umiSeq,umiB)!=-1) {//convert and check for Ns
        stats.V[stats.nNinBarcode]++;//UMIs are not allowed to have Ns
        return false;
    };
    if (umiB==homoPolymer[0] || umiB==homoPolymer[1] || umiB==homoPolymer[2] || umiB==homoPolymer[3]) {
        stats.V[stats.nUMIhomopolymer]++;
        return false;
    };
    return true;
};
void SoloReadBarcode::getCBandUMI(string &readNameExtra)
{
    if (pSolo.type==0)
        return;

    cbMatch=-1;
    cbMatchString="";
    cbMatchInd.clear();
    
    uint32 bLength = readNameExtra.find(' ',pSolo.bL);
    string bSeq=readNameExtra.substr(0,bLength);
    string bQual=readNameExtra.substr(bLength+1,bLength);

    if (pSolo.type==1) {
        cbSeq=bSeq.substr(pSolo.cbS-1,pSolo.cbL);
        umiSeq=bSeq.substr(pSolo.umiS-1,pSolo.umiL);
        cbQual=bQual.substr(pSolo.cbS-1,pSolo.cbL);
        umiQual=bQual.substr(pSolo.umiS-1,pSolo.umiL);
        
        if (!convertCheckUMI())
            return;
        
        matchCBtoWL(cbSeq, cbQual, pSolo.cbWL, cbMatch, cbMatchInd, cbMatchString);
        if (cbMatch==0) //exact match
            cbReadCountExact[cbMatchInd[0]]++;//note that this simply counts reads per exact CB, no checks of genes or UMIs

    } else if (pSolo.type==2) {
        
        uint32 adapterStart=0;
        if (pSolo.adapterYes) {
            if (localAlignHammingDist(bSeq, pSolo.adapterSeq, adapterStart) > pSolo.adapterMismatchesNmax) {
                //TODO: add stats
                return; //no adapter found
            };
        };

        if (!pSolo.umiV.extractBarcode(bSeq, bQual, adapterStart, umiSeq, umiQual)) {
            //TODO: add stats
            return;
        };

        if (!convertCheckUMI())
            return;        
        
        cbSeq="";
        cbQual="";
        bool cbMatchGood=true;
        cbMatchInd={0};
        for (auto &cb : pSolo.cbV) {//cycle over multiple barcodes
            
            string cbSeq1, cbQual1;
            if (!cb.extractBarcode(bSeq, bQual, adapterStart, cbSeq1, cbQual1)) {
                //TODO: add stats
                return;
            };
            cbSeq  += cbSeq1 + "_";
            cbQual += cbQual1 + "_";
            
            if (!cbMatchGood || cbSeq1.size() < cb.minLen || cbSeq1.size() >= cb.wl.size() || cb.wl[cbSeq1.size()].size()==0) {//no match possible for this barcode, or no match for previous barcodes
                cbMatchGood=false;
                continue; //continue - to be able to record full cbSeq, cbQual
            };
            
            int32 cbMatch1;
            vector<uint64> cbMatchInd1={};
//             cbMatchInd1.reserve(4);
            matchCBtoWL(cbSeq1, cbQual1, cb.wl[cbSeq1.size()], cbMatch1, cbMatchInd1, cbMatchString); //cbMatchString is not used for now, multiple matches are not allowed
            if (cbMatch1<0 || (cbMatch1>0 && cbMatch>0)) {//this barcode has >1 1MM match, or previous barcode had a mismatch
                cbMatchGood=false;
            } else {
                cbMatchInd[0] += cb.wlFactor*(cbMatchInd1[0]+cb.wlAdd[cbSeq1.size()]);
            };
            cbMatch=max(cbMatch,cbMatch1);//1 wins over 0
        };
        cbSeq.pop_back();//remove last "_" from file
        cbQual.pop_back();
        
        if (cbMatchGood) {
            if (cbMatch==0) //exact match
                cbReadCountExact[cbMatchInd[0]]++;//note that this simply counts reads per exact CB, no checks of genes or UMIs
            cbMatchString=to_string(cbMatchInd[0]);
        } else {
            cbMatch=-1;
        };
    };
};
