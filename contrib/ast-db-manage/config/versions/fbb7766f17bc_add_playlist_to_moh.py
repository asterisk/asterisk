"""add playlist to moh

Revision ID: fbb7766f17bc
Revises: 3a094a18e75b
Create Date: 2019-09-18 10:24:18.731798

"""

# revision identifiers, used by Alembic.
revision = 'fbb7766f17bc'
down_revision = '3a094a18e75b'

from alembic import op
import sqlalchemy as sa


def enum_update(table_name, column_name, enum_name, enum_values):
    if op.get_context().bind.dialect.name != 'postgresql':
        if op.get_context().bind.dialect.name == 'mssql':
            op.drop_constraint('ck_musiconhold_mode_moh_mode_values', 'musiconhold')
        op.alter_column(table_name, column_name,
                        type_=sa.Enum(*enum_values, name=enum_name))
        return

    # Postgres requires a few more steps
    tmp = enum_name + '_tmp'

    op.execute('ALTER TYPE ' + enum_name + ' RENAME TO ' + tmp)

    updated = sa.Enum(*enum_values, name=enum_name)
    updated.create(op.get_bind(), checkfirst=False)

    op.execute('ALTER TABLE ' + table_name + ' ALTER COLUMN ' + column_name +
               ' TYPE ' + enum_name + ' USING mode::text::' + enum_name)

    op.execute('DROP TYPE ' + tmp)


def upgrade():
    op.create_table(
        'musiconhold_entry',
        sa.Column('name', sa.String(80), primary_key=True, nullable=False),
        sa.Column('position', sa.Integer, primary_key=True, nullable=False),
        sa.Column('entry', sa.String(1024), nullable=False)
    )
    op.create_foreign_key('fk_musiconhold_entry_name_musiconhold', 'musiconhold_entry', 'musiconhold', ['name'], ['name'])
    enum_update('musiconhold', 'mode', 'moh_mode_values',
                ['custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3', 'playlist'])


def downgrade():
    enum_update('musiconhold', 'mode', 'moh_mode_values',
                ['custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3'])
    op.drop_table('musiconhold_entry')
