#include "SpliceGraph.h"
#include "sjAlignSplit.h"

void SpliceGraph::findSuperTr(const char *readSeq, const char *readSeqRevCompl, const uint32 readLen, const string &readName, Genome &mapGen)
{//find the candidate superTranscripts: seed-and-rank algorithm implemented
		
	//hardcoded for now
	float seedCoverageThreshold = 0.1;
	float seedCoverageMinToMax = 0.1;
	uint32 seedMultMax = 200;
	
	vector<uint32> seedSuperTr;
	seedSuperTr.reserve(seedMultMax);
	
	//readLen, readSeq
    uint32 seedLen=mapGen.pGe.gSAindexNbases; //TODO: make user-definable
    memset(superTrSeedCount,0,sizeof(superTrSeedCount[0])*2*superTr.N);
    
    for (uint32 iseed=0; iseed<readLen/seedLen; iseed++) {//loop through seeds
        //calculate index
        uint64 ind1=0;
        for (uint32 ii=iseed*seedLen; ii<(iseed+1)*seedLen; ii++) {
            uint b=(uint64) readSeq[ii];
            if (b>3) {//N
                continue; //no mapping for seeds with Ns
            } else {
                ind1 <<= 2;
                ind1 += b;
            };
        };
        //find seed boundaries in SA
        uint64 iSA1=mapGen.SAi[mapGen.genomeSAindexStart[seedLen-1]+ind1]; // starting point for suffix array search.
        if ( (iSA1 & mapGen.SAiMarkAbsentMaskC) != 0) {//prefix does not exist, skip this seed
            continue;
        };
        uint64 iSA2;
        if (mapGen.genomeSAindexStart[seedLen-1]+ind1+1 < mapGen.genomeSAindexStart[seedLen]) {//we are not at the end of the SA
            iSA2=((mapGen.SAi[mapGen.genomeSAindexStart[seedLen-1]+ind1+1] & mapGen.SAiMarkNmask) & mapGen.SAiMarkAbsentMask) - 1;
        } else {
            iSA2=mapGen.nSA-1;
        };

		if (iSA2-iSA1>=seedMultMax) //this seed map too many times
			continue;
		
		seedSuperTr.clear();
		seedSuperTr.resize(iSA2-iSA1+1);
        for (uint64 isa=iSA1;isa<=iSA2;isa++) {//loop through seed SA boundaries
            uint64 a1 = mapGen.SA[isa];
            uint64 aStr = a1 >> mapGen.GstrandBit;
            a1 = a1 & mapGen.GstrandMask; //remove strand bit
            if (aStr==1)
                a1 = mapGen.nGenome - (seedLen+a1);
            
            if (a1>=mapGen.sjGstart) {//this is sj align. Probably do not need it...
                uint64 a1D, aLengthD, a1A, aLengthA, sj1;
                if ( sjAlignSplit(a1, seedLen, mapGen, a1D, aLengthD, a1A, aLengthA, sj1) ) {//if align crosses the junction, replace a1 with a1D
                    a1=a1D;
                } else {
                    continue;//this align does not cross the junction, no need to contine
                };
            };
            
            seedSuperTr[isa-iSA1]=(uint32)(aStr*superTr.N  + mapGen.chrBin[a1 >> mapGen.pGe.gChrBinNbits]);
        };//loop through seed SA boundaries
		
		sort(seedSuperTr.begin(),seedSuperTr.end());
		uint32 su1prev=(uint32)-1;
		for (auto &su1 : seedSuperTr) {//this will eliminate multiple matches of the same seed into the same suTr
			if (su1!=su1prev) {
				superTrSeedCount[su1]++;
				su1prev=su1;
			};
		};
			
    };//loop through seeds
	
    //find max coverage
    typeSuperTrSeedCount countMax=0;
	//float countOverSuperTrLenMax=0;
    for (uint32 ii=0; ii<2*superTr.N; ii++) {
        countMax=max(superTrSeedCount[ii], countMax);
		//countOverSuperTrLenMax=max(superTrSeedCount[ii]/float(superTr.length[ii%superTr.N]), countOverSuperTrLenMax);
    };
    
    if (countMax*seedLen<readLen*seedCoverageThreshold)//no good candidates, hard-coded for now
        return;
    
    for (uint32 ii=0; ii<superTr.N; ii++) {//selection cycle
				
        uint32 sutr1=ii%superTr.N;
        uint32 str1=ii/superTr.N;
		
        //if (superTrSeedCount[ii]<countMax*seedCoverageMinToMax)
		if (superTrSeedCount[ii] < readLen*seedCoverageThreshold/seedLen)
		//if (superTrSeedCount[ii]<countOverSuperTrLenMax*superTr.length[sutr1]*seedCoverageMinToMax)
			continue;


		if (superTr.length[sutr1]>=100000) //temporary restriction: will lift after redoing the scoringMatrix
			continue;		
		
        array<uint32,2> alignEnds, alignStarts;
        
		uint32 swScore = 0;
        swScore = swScoreSpliced((str1==0 ? readSeq : readSeqRevCompl), readLen, sutr1, alignEnds);
        
        //swTraceBack(alignEnds, alignStarts);
		//float(superTrSeedCount[ii])/countMax
		//float(superTrSeedCount[ii])/superTr.length[sutr1]/countOverSuperTrLenMax <<'\t'<<

		cout << readName <<'\t'<< sutr1 <<'\t'<< str1 <<'\t'<< superTr.length[sutr1] <<'\t'<< readLen <<'\t'<< float(superTrSeedCount[ii])/readLen*seedLen <<'\t'<<  
				float(superTrSeedCount[ii])/countMax <<'\t'<<
                swScore <<'\t'<< float(swScore)/readLen <<'\t'<< alignStarts[0] <<'\t'<< alignEnds[0] <<'\t'<< alignStarts[1] <<'\t'<< alignEnds[1] << endl;

    };

    return;
};
