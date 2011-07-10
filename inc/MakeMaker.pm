package inc::MakeMaker;
use Moose;

extends 'Dist::Zilla::Plugin::MakeMaker::Awesome';

override _build_MakeFile_PL_template => sub {
    my $self = shift;

    my $tmpl = super;

    my $depends = <<'END';
if ($] < 5.014) {
    %WriteMakefileArgs = (
        %WriteMakefileArgs,
        XS => {},
        C  => [],
    );
}
END

    $tmpl =~ s/(WriteMakefile\()/$depends\n$1/;

    return $tmpl;
};

__PACKAGE__->meta->make_immutable;
1;
