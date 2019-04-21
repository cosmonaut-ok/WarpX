#include <WarpXConst.H>
#include <SpectralKSpace.H>
#include <cmath>

using namespace amrex;
using namespace Gpu;

SpectralKSpace::SpectralKSpace( const BoxArray& realspace_ba,
                                const DistributionMapping& dm,
                                const RealVect realspace_dx )
{
    // Store the cell size
    dx = realspace_dx;

    // Create the box array that corresponds to spectral space
    BoxList spectral_bl; // Create empty box list
    // Loop over boxes and fill the box list
    for (int i=0; i < realspace_ba.size(); i++ ) {
        // For local FFTs, each box in spectral space starts at 0 in each direction
        // and has the same number of points as the real space box (including guard cells)
        Box realspace_bx = realspace_ba[i];
        Box bx = Box( IntVect::TheZeroVector(), realspace_bx.bigEnd() - realspace_bx.smallEnd() );
        spectral_bl.push_back( bx );
    }
    spectralspace_ba.define( spectral_bl );

    // Allocate the components of the k vector: kx, ky (only in 3D), kz
    for (int i_dim=0; i_dim<AMREX_SPACEDIM; i_dim++) {
        k_vec[i_dim] = AllocateAndFillKComponent( dm, i_dim );
    }
}

KVectorComponent
SpectralKSpace::AllocateAndFillKComponent( const DistributionMapping& dm, const int i_dim ) const
{
    // Initialize an empty ManagedVector in each box
    KVectorComponent k_comp = KVectorComponent(spectralspace_ba, dm);
    // Loop over boxes
    for ( MFIter mfi(spectralspace_ba, dm); mfi.isValid(); ++mfi ){
        Box bx = spectralspace_ba[mfi];
        ManagedVector<Real>& k = k_comp[mfi];

        // Allocate k to the right size
        int N = bx.length( i_dim );
        k.resize( N );

        // Fill the k vector
        const Real dk = 2*MathConst::pi/(N*dx[i_dim]);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE( bx.smallEnd(i_dim) == 0,
            "Expected box to start at 0, in spectral space.");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE( bx.bigEnd(i_dim) == N-1,
            "Expected different box end index in spectral space.");
        // Fill positive values of k (FFT conventions: first half is positive)
        for (int i=0; i<(N+1)/2; i++ ){
            k[i] = i*dk;
        }
        // Fill negative values of k (FFT conventions: second half is negative)
        for (int i=(N+1)/2; i<N; i++){
            k[i] = (N-i)*dk;
        }
        // TODO: This should be quite different for the hybrid spectral code:
        // In that case we should take into consideration the actual indices of the box
        // and distinguish the size of the local box and that of the global FFT
        // This will also be different for the real-to-complex transform
    }
    return k_comp;
}

KVectorComponent
SpectralKSpace::AllocateAndFillModifiedKComponent(
        const DistributionMapping& dm, const int i_dim, const int order ) const
{
    // Initialize an empty ManagedVector in each box
    KVectorComponent modified_k_comp = KVectorComponent( spectralspace_ba, dm );
    // Loop over boxes
    for ( MFIter mfi(spectralspace_ba, dm); mfi.isValid(); ++mfi ){
        const ManagedVector<Real>& k = k_vec[i_dim][mfi];
        ManagedVector<Real>& modified_k = modified_k_comp[mfi];

        // Allocate modified_k to the same size as k
        modified_k.resize( k.size() );

        // Fill the modified k vector
        for (int i=0; i<k.size(); i++ ){
            // For now, this simply copies the infinite order k
            // TODO: Use the formula for finite-order modified k vector
            modified_k[i] = k[i];
        }
    }
    return modified_k_comp;
}

SpectralShiftFactor
SpectralKSpace::AllocateAndFillSpectralShiftFactor(
        const DistributionMapping& dm, const int i_dim, const int shift_type ) const
{
    // Initialize an empty ManagedVector in each box
    SpectralShiftFactor shift_factor = SpectralShiftFactor( spectralspace_ba, dm );
    // Loop over boxes
    for ( MFIter mfi(spectralspace_ba, dm); mfi.isValid(); ++mfi ){
        const ManagedVector<Real>& k = k_vec[i_dim][mfi];
        ManagedVector<Complex>& shift = shift_factor[mfi];

        // Allocate shift coefficients
        shift.resize( k.size() );

        // Fill the shift coefficients
        Real sign = 0;
        switch (shift_type){
            case ShiftType::CenteredToNodal: sign = -1.;
            case ShiftType::NodalToCentered: sign = 1.;
        }
        constexpr Complex I{0,1};
        for (int i=0; i<k.size(); i++ ){
            shift[i] = std::exp( I*sign*k[i]*0.5*dx[i_dim] );
        }
    }
    return shift_factor;
}
