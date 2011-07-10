package inc::MakeMaker;
use Moose;

extends 'Dist::Zilla::Plugin::MakeMaker::Awesome';

override _build_MakeFile_PL_template => sub {
    my $self = shift;

    my $tmpl = super;

    my $depends = <<'END';
%WriteMakefileArgs = (
    %WriteMakefileArgs,
    Devel::CallChecker::callchecker_linkable,
);
END

    $tmpl =~ s/(use ExtUtils.*)/$1\nuse Devel::CallChecker;/;
    $tmpl =~ s/(WriteMakefile\()/$depends\n$1/;
    $tmpl .= <<'END';
open my $header, '>', 'callchecker0.h'
    or die "Couldn't open callchecker0.h for writing: $!";
print $header Devel::CallChecker::callchecker0_h;
close $header;
END

    return $tmpl;
};

__PACKAGE__->meta->make_immutable;
1;
