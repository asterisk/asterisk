"""Add auto DTMF mode

Revision ID: 31cd4f4891ec
Revises: 23530d604b96
Create Date: 2015-04-10 12:36:51.619419

"""

# revision identifiers, used by Alembic.
revision = '31cd4f4891ec'
down_revision = '23530d604b96'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa

OLD_ENUM = ['rfc4733', 'inband', 'info']
NEW_ENUM = ['rfc4733', 'inband', 'info', 'auto']

old_type = sa.Enum(*OLD_ENUM, name='pjsip_dtmf_mode_values')
new_type = sa.Enum(*NEW_ENUM, name='pjsip_dtmf_mode_values_v2')

tcr = sa.sql.table('ps_endpoints', sa.Column('dtmf_mode', new_type,
                   nullable=True))

def upgrade():
    context = op.get_context()

    # Upgrading to this revision WILL clear your directmedia values.
    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', 'dtmf_mode',
                        type_=new_type,
                        existing_type=old_type)
    else:
        enum = ENUM('rfc4733', 'inband', 'info', 'auto',
                    name='pjsip_dtmf_mode_values_v2')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values_v2 USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values_v2')

        ENUM(name="pjsip_dtmf_mode_values").drop(op.get_bind(), checkfirst=False)

def downgrade():
    context = op.get_context()

    op.execute(tcr.update().where(tcr.c.directmedia==u'outgoing')
               .values(directmedia=None))

    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', 'dtmf_mode',
                        type_=old_type,
                        existing_type=new_type)
    else:
        enum = ENUM('rfc4733', 'inband', 'info',
                    name='pjsip_dtmf_mode_values')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values')

        ENUM(name="pjsip_dtmf_mode_values_v2").drop(op.get_bind(), checkfirst=False)
